// Deterministic, read-only export for the GhidraCalibur browse workspace.

import java.io.BufferedOutputStream;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import generic.cache.CachingPool;
import generic.cache.CountingBasicFactory;
import generic.concurrent.QCallback;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompiledFunction;
import ghidra.app.decompiler.parallel.ChunkingParallelDecompiler;
import ghidra.app.decompiler.parallel.ParallelDecompiler;
import ghidra.app.script.GhidraScript;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.Composite;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.Enum;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.util.DefinedDataIterator;
import ghidra.util.exception.CancelledException;
import ghidra.util.task.TaskMonitor;

public class StructuredExporter extends GhidraScript {
    private static final String SCHEMA = "ghidra-calibur-export/v1";
    private static final int DECOMPILE_CHUNK_SIZE = 2000;
    private long startModificationNumber;

    private static final class PeExport {
        final long rva;
        final long ordinal;
        final String name;

        PeExport(long rva, long ordinal, String name) {
            this.rva = rva;
            this.ordinal = ordinal;
            this.name = name;
        }
    }

    private static final class BodyResult implements Comparable<BodyResult> {
        final Function function;
        final String status;
        final byte[] body;
        final String diagnostic;
        final long durationMs;

        BodyResult(Function function, String status, byte[] body, String diagnostic, long durationMs) {
            this.function = function;
            this.status = status;
            this.body = body;
            this.diagnostic = diagnostic;
            this.durationMs = durationMs;
        }

        @Override
        public int compareTo(BodyResult other) {
            return function.getEntryPoint().compareTo(other.function.getEntryPoint());
        }
    }

    private static final class WrittenFunction {
        final Function function;
        BodyResult result;
        long bodyOffset;
        long bodyLength;
        String bodyHash;

        WrittenFunction(Function function, BodyResult result, long bodyOffset,
                long bodyLength, String bodyHash) {
            this.function = function;
            this.result = result;
            this.bodyOffset = bodyOffset;
            this.bodyLength = bodyLength;
            this.bodyHash = bodyHash;
        }
    }

    private static final class CommentRecord implements Comparable<CommentRecord> {
        final Address address;
        final String functionAddress;
        final String type;
        final int typeOrder;
        final String text;

        CommentRecord(Address address, String functionAddress, String type, int typeOrder, String text) {
            this.address = address;
            this.functionAddress = functionAddress;
            this.type = type;
            this.typeOrder = typeOrder;
            this.text = text;
        }

        @Override
        public int compareTo(CommentRecord other) {
            int space = address.getAddressSpace().getName().compareTo(other.address.getAddressSpace().getName());
            if (space != 0) return space;
            int offset = address.compareTo(other.address);
            return offset != 0 ? offset : Integer.compare(typeOrder, other.typeOrder);
        }
    }

    private final class DecompilerFactory extends CountingBasicFactory<DecompInterface> {
        private final DecompileOptions options;

        DecompilerFactory(DecompileOptions options) {
            this.options = options;
        }

        @Override
        public DecompInterface doCreate(int itemNumber) {
            DecompInterface decompiler = new DecompInterface();
            decompiler.setOptions(options);
            decompiler.toggleSyntaxTree(true);
            decompiler.toggleCCode(true);
            decompiler.openProgram(currentProgram);
            return decompiler;
        }

        @Override
        public void doDispose(DecompInterface decompiler) {
            decompiler.dispose();
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            throw new IllegalArgumentException(
                "Usage: StructuredExporter.java <output-directory> [timeout-seconds]");
        }

        File outputDir = new File(args[0]).getCanonicalFile();
        int timeoutSeconds = args.length >= 2 ? Integer.parseInt(args[1]) : 5;
        if (timeoutSeconds < 1) {
            throw new IllegalArgumentException("timeout-seconds must be positive");
        }
        if (!outputDir.exists() && !outputDir.mkdirs()) {
            throw new IllegalStateException("Unable to create output directory: " + outputDir);
        }
        startModificationNumber = currentProgram.getModificationNumber();
        if (args.length >= 3 && "--probe".equals(args[2])) {
            exportProbe(new File(outputDir, "probe.json"));
            println("Structured export probe finished: " + outputDir);
            return;
        }

        File functionsFile = new File(outputDir, "functions.jsonl");
        File diagnosticsFile = new File(outputDir, "diagnostics.jsonl");
        File bodiesFile = new File(outputDir, "bodies.dat");

        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        options.setDefaultTimeout(timeoutSeconds);

        Map<String, Long> counts = new LinkedHashMap<>();
        long started = System.currentTimeMillis();
        exportFunctions(functionsFile, diagnosticsFile, bodiesFile, options, timeoutSeconds, counts);
        exportTypes(new File(outputDir, "types.jsonl"), counts);
        exportGlobals(new File(outputDir, "globals.jsonl"), counts);
        exportStrings(new File(outputDir, "strings.jsonl"), counts);
        exportComments(new File(outputDir, "comments.jsonl"), counts);
        exportCalls(new File(outputDir, "calls.jsonl"), counts);
        exportImports(new File(outputDir, "imports.jsonl"), counts);
        exportExports(new File(outputDir, "exports.jsonl"), counts);
        if (currentProgram.getModificationNumber() != startModificationNumber) {
            throw new IllegalStateException(
                "Program modification number changed during export (" + startModificationNumber +
                " -> " + currentProgram.getModificationNumber() + ")");
        }
        exportInfo(new File(outputDir, "export_info.json"), options, timeoutSeconds, counts);

        long elapsed = System.currentTimeMillis() - started;
        println("Structured export finished in " + elapsed + " ms: " + outputDir);
    }

    private void exportProbe(File output) throws Exception {
        String value = "{" +
            field("schema", SCHEMA) + "," +
            field("program_name", currentProgram.getName()) + "," +
            field("program_path", currentProgram.getDomainFile().getPathname()) + "," +
            field("executable_sha256", nullToEmpty(currentProgram.getExecutableSHA256())) + "," +
            field("image_base", currentProgram.getImageBase().toString(false, false)) + "," +
            field("ghidra_version", Application.getApplicationVersion()) + "," +
            numberField("modification_number", startModificationNumber) +
            "}";
        try (BufferedWriter writer = writer(output)) {
            writer.write(value);
            writer.newLine();
        }
    }

    private void exportFunctions(File functionsFile, File diagnosticsFile, File bodiesFile,
            DecompileOptions options, int timeoutSeconds, Map<String, Long> counts) throws Exception {
        long bodyOffset = 0;
        Map<String, Long> statuses = new LinkedHashMap<>();
        List<WrittenFunction> written = new ArrayList<>();
        CachingPool<DecompInterface> pool = new CachingPool<>(new DecompilerFactory(options));
        QCallback<Function, BodyResult> callback = (function, callbackMonitor) ->
            decompileOne(function, timeoutSeconds, callbackMonitor, pool);

        ChunkingParallelDecompiler<BodyResult> parallel =
            ParallelDecompiler.createChunkingParallelDecompiler(callback, monitor);

        try (OutputStream bodies = new BufferedOutputStream(new FileOutputStream(bodiesFile))) {
            List<Function> chunk = new ArrayList<>(DECOMPILE_CHUNK_SIZE);
            for (Function function : allFunctions()) {
                monitor.checkCancelled();
                chunk.add(function);
                if (chunk.size() >= DECOMPILE_CHUNK_SIZE) {
                    List<BodyResult> results = parallel.decompileFunctions(chunk);
                    Collections.sort(results);
                    bodyOffset = storeFunctionResults(results, bodies, bodyOffset, written);
                    chunk.clear();
                }
            }
            if (!chunk.isEmpty()) {
                List<BodyResult> results = parallel.decompileFunctions(chunk);
                Collections.sort(results);
                bodyOffset = storeFunctionResults(results, bodies, bodyOffset, written);
            }
            bodyOffset = retryFailedFunctions(written, bodies, bodyOffset, timeoutSeconds, pool, counts);
        }
        finally {
            pool.dispose();
            parallel.dispose();
        }

        try (BufferedWriter functions = writer(functionsFile);
             BufferedWriter diagnostics = writer(diagnosticsFile)) {
            for (WrittenFunction record : written) {
                writeFunctionRecord(record, functions, diagnostics);
                String status = record.result.status;
                statuses.put(status, statuses.getOrDefault(status, 0L) + 1);
            }
        }
        ensureWriterSucceeded(functionsFile);
        ensureWriterSucceeded(diagnosticsFile);
        counts.put("functions", (long) written.size());
        counts.put("body_bytes", bodyOffset);
        for (Map.Entry<String, Long> status : statuses.entrySet()) {
            counts.put("status_" + status.getKey().replace('-', '_'), status.getValue());
        }
    }

    private BodyResult decompileOne(Function function, int timeoutSeconds,
            TaskMonitor callbackMonitor, CachingPool<DecompInterface> pool) throws Exception {
        if (callbackMonitor.isCancelled()) {
            return new BodyResult(function, "cancelled", null, "cancelled", 0);
        }
        if (function.isExternal()) {
            return new BodyResult(function, "external", null, "", 0);
        }
        CodeUnit unit = currentProgram.getListing().getCodeUnitAt(function.getEntryPoint());
        if (!(unit instanceof Instruction)) {
            return new BodyResult(function, "no-instruction", null, "", 0);
        }

        long started = System.nanoTime();
        DecompInterface decompiler = pool.get();
        try {
            DecompileResults results = decompiler.decompileFunction(
                function, timeoutSeconds, callbackMonitor);
            long durationMs = (System.nanoTime() - started) / 1_000_000L;
            String error = results.getErrorMessage();
            if (error != null && !error.isBlank()) {
                String status = error.toLowerCase().contains("timeout")
                    ? "timeout" : "decompile-error";
                return new BodyResult(function, status, null, error, durationMs);
            }
            DecompiledFunction decompiled = results.getDecompiledFunction();
            if (decompiled == null || decompiled.getC() == null) {
                return new BodyResult(function, "decompile-error", null,
                    "Decompiler returned no C body", durationMs);
            }
            byte[] body = decompiled.getC().getBytes(StandardCharsets.UTF_8);
            return new BodyResult(function, "ok", body, "", durationMs);
        }
        catch (Exception exc) {
            long durationMs = (System.nanoTime() - started) / 1_000_000L;
            return new BodyResult(function, "decompile-error", null,
                exc.getClass().getSimpleName() + ": " + safeMessage(exc), durationMs);
        }
        finally {
            pool.release(decompiler);
        }
    }

    private long storeFunctionResults(List<BodyResult> results, OutputStream bodies,
            long bodyOffset, List<WrittenFunction> written) throws Exception {
        for (BodyResult result : results) {
            monitor.checkCancelled();
            byte[] body = result.body;
            long length = body == null ? 0 : body.length;
            String bodyHash = body == null ? "" : sha256(body);
            long recordOffset = body == null ? -1 : bodyOffset;
            if (body != null) {
                bodies.write(body);
                bodyOffset += body.length;
            }
            written.add(new WrittenFunction(
                result.function, result, recordOffset, length, bodyHash));
        }
        return bodyOffset;
    }

    private long retryFailedFunctions(List<WrittenFunction> written, OutputStream bodies,
            long bodyOffset, int timeoutSeconds, CachingPool<DecompInterface> pool,
            Map<String, Long> counts) throws Exception {
        List<Function> failures = new ArrayList<>();
        Map<String, WrittenFunction> records = new LinkedHashMap<>();
        for (WrittenFunction record : written) {
            if ("timeout".equals(record.result.status) ||
                    "decompile-error".equals(record.result.status)) {
                failures.add(record.function);
                records.put(record.function.getEntryPoint().toString(true), record);
            }
        }
        if (failures.isEmpty()) {
            counts.put("retry_attempts", 0L);
            return bodyOffset;
        }

        counts.put("retry_attempts", (long) failures.size());

        int retryTimeout = Math.max(20, timeoutSeconds * 4);
        QCallback<Function, BodyResult> retryCallback = (function, callbackMonitor) ->
            decompileOne(function, retryTimeout, callbackMonitor, pool);
        ChunkingParallelDecompiler<BodyResult> retryParallel =
            ParallelDecompiler.createChunkingParallelDecompiler(retryCallback, monitor);
        try {
            List<BodyResult> retried = retryParallel.decompileFunctions(failures);
            Collections.sort(retried);
            for (BodyResult result : retried) {
                monitor.checkCancelled();
                WrittenFunction record = records.get(result.function.getEntryPoint().toString(true));
                if (record == null) {
                    throw new IllegalStateException(
                        "Retry produced an unknown function: " + result.function.getEntryPoint());
                }
                record.result = result;
                if (result.body != null) {
                    record.bodyOffset = bodyOffset;
                    record.bodyLength = result.body.length;
                    record.bodyHash = sha256(result.body);
                    bodies.write(result.body);
                    bodyOffset += result.body.length;
                }
                else {
                    record.bodyOffset = -1;
                    record.bodyLength = 0;
                    record.bodyHash = "";
                }
            }
        }
        finally {
            retryParallel.dispose();
        }
        return bodyOffset;
    }

    private void writeFunctionRecord(WrittenFunction record, BufferedWriter functions,
            BufferedWriter diagnostics) throws Exception {
            Function function = record.function;
            BodyResult result = record.result;

            Function thunk = function.getThunkedFunction(true);
            String namespace = function.getParentNamespace() == null ? ""
                : function.getParentNamespace().getName(true);
            String json = "{" +
                field("address_space", function.getEntryPoint().getAddressSpace().getName()) + "," +
                field("address", function.getEntryPoint().toString(false, false)) + "," +
                field("name", function.getName()) + "," +
                field("qualified_name", function.getName(true)) + "," +
                field("namespace", namespace) + "," +
                field("signature", function.getSignature().getPrototypeString()) + "," +
                field("calling_convention", nullToEmpty(function.getCallingConventionName())) + "," +
                field("thunk_target", thunk == null ? "" : thunk.getEntryPoint().toString(false, false)) + "," +
                boolField("is_thunk", function.isThunk()) + "," +
                boolField("is_external", function.isExternal()) + "," +
                field("external_library", function.isExternal() ? externalLibraryName(function) : "") + "," +
                boolField("no_return", function.hasNoReturn()) + "," +
                field("body_status", result.status) + "," +
                numberField("body_offset", record.bodyOffset) + "," +
                numberField("body_length", record.bodyLength) + "," +
                field("body_sha256", record.bodyHash) +
                "}";
            functions.write(json);
            functions.newLine();

            diagnostics.write("{" +
                field("address", function.getEntryPoint().toString(false, false)) + "," +
                numberField("duration_ms", result.durationMs) + "," +
                field("message", result.diagnostic) +
                "}");
            diagnostics.newLine();
    }

    private void exportTypes(File output, Map<String, Long> counts) throws Exception {
        List<DataType> types = new ArrayList<>();
        Iterator<DataType> iterator = currentProgram.getDataTypeManager().getAllDataTypes();
        while (iterator.hasNext()) {
            types.add(iterator.next());
        }
        types.sort(Comparator.comparing((DataType type) -> type.getCategoryPath().getPath())
            .thenComparing(DataType::getName));

        long count = 0;
        try (BufferedWriter out = writer(output)) {
            for (DataType type : types) {
                monitor.checkCancelled();
                String kind = typeKind(type);
                StringBuilder json = new StringBuilder();
                json.append("{")
                    .append(field("category", type.getCategoryPath().getPath())).append(",")
                    .append(field("name", type.getName())).append(",")
                    .append(field("display_name", type.getDisplayName())).append(",")
                    .append(field("kind", kind)).append(",")
                    .append(numberField("length", type.getLength())).append(",")
                    .append(numberField("alignment", type.getAlignment())).append(",")
                    .append(field("description", nullToEmpty(type.getDescription())));

                if (type instanceof Composite composite) {
                    json.append(",\"components\":[");
                    DataTypeComponent[] components = composite.getDefinedComponents();
                    for (int index = 0; index < components.length; index++) {
                        if (index > 0) json.append(',');
                        DataTypeComponent component = components[index];
                        json.append("{")
                            .append(numberField("offset", component.getOffset())).append(",")
                            .append(numberField("length", component.getLength())).append(",")
                            .append(field("name", nullToEmpty(component.getFieldName()))).append(",")
                            .append(field("type", component.getDataType().getDisplayName())).append(",")
                            .append(field("comment", nullToEmpty(component.getComment())))
                            .append("}");
                    }
                    json.append(']');
                }
                else if (type instanceof Enum enumType) {
                    json.append(",\"values\":[");
                    String[] names = enumType.getNames();
                    java.util.Arrays.sort(names);
                    for (int index = 0; index < names.length; index++) {
                        if (index > 0) json.append(',');
                        json.append("{").append(field("name", names[index])).append(",")
                            .append(numberField("value", enumType.getValue(names[index]))).append("}");
                    }
                    json.append(']');
                }
                else if (type instanceof TypeDef typeDef) {
                    json.append(',').append(field("base_type", typeDef.getBaseDataType().getDisplayName()));
                }
                else if (type instanceof Pointer pointer) {
                    DataType target = pointer.getDataType();
                    json.append(',').append(field("target_type", target == null ? "void" : target.getDisplayName()));
                }
                else if (type instanceof Array array) {
                    json.append(',').append(field("element_type", array.getDataType().getDisplayName()))
                        .append(',').append(numberField("element_count", array.getNumElements()));
                }
                json.append("}");
                out.write(json.toString());
                out.newLine();
                count++;
            }
        }
        counts.put("types", count);
    }

    private void exportGlobals(File output, Map<String, Long> counts) throws Exception {
        long count = 0;
        Listing listing = currentProgram.getListing();
        try (BufferedWriter out = writer(output)) {
            DataIterator iterator = listing.getDefinedData(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Data data = iterator.next();
                Symbol symbol = data.getPrimarySymbol();
                if (data.hasStringValue()) {
                    continue;
                }
                List<Function> referrers = referringFunctions(data.getAddress());
                out.write("{" + field("address_space", data.getAddress().getAddressSpace().getName()) + "," +
                    field("address", data.getAddress().toString(false, false)) + "," +
                    field("name", symbol == null ? "" : symbol.getName()) + "," +
                    field("qualified_name", symbol == null ? "" : symbol.getName(true)) + "," +
                    field("type", data.getDataType().getDisplayName()) + "," +
                    numberField("length", data.getLength()) + "," +
                    arrayField("referring_functions", functionAddresses(referrers)) + "," +
                    arrayField("referring_function_address_spaces", functionAddressSpaces(referrers)) +
                    "}");
                out.newLine();
                count++;
            }
        }
        counts.put("globals", count);
    }

    private void exportStrings(File output, Map<String, Long> counts) throws Exception {
        long count = 0;
        try (BufferedWriter out = writer(output)) {
            DataIterator iterator = currentProgram.getListing().getDefinedData(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Data data = iterator.next();
                if (!data.hasStringValue()) {
                    continue;
                }
                Object value = data.getValue();
                List<Function> referrers = referringFunctions(data.getAddress());
                out.write("{" + field("address_space", data.getAddress().getAddressSpace().getName()) + "," +
                    field("address", data.getAddress().toString(false, false)) + "," +
                    field("value", value == null ? "" : value.toString()) + "," +
                    field("type", data.getDataType().getDisplayName()) + "," +
                    arrayField("referring_functions", functionAddresses(referrers)) + "," +
                    arrayField("referring_function_address_spaces", functionAddressSpaces(referrers)) +
                    "}");
                out.newLine();
                count++;
            }
        }
        counts.put("strings", count);
    }

    private List<Function> referringFunctions(Address address) {
        List<Function> referrers = new ArrayList<>();
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
        while (references.hasNext()) {
            Reference reference = references.next();
            Function function = currentProgram.getFunctionManager()
                .getFunctionContaining(reference.getFromAddress());
            if (function != null) {
                if (!referrers.contains(function)) {
                    referrers.add(function);
                }
            }
        }
        referrers.sort(Comparator.comparing(Function::getEntryPoint));
        return referrers;
    }

    private List<String> functionAddresses(List<Function> functions) {
        List<String> result = new ArrayList<>();
        for (Function function : functions) result.add(function.getEntryPoint().toString(false, false));
        return result;
    }

    private List<String> functionAddressSpaces(List<Function> functions) {
        List<String> result = new ArrayList<>();
        for (Function function : functions) result.add(function.getEntryPoint().getAddressSpace().getName());
        return result;
    }

    private void exportComments(File output, Map<String, Long> counts) throws Exception {
        List<CommentRecord> records = new ArrayList<>();
        Listing listing = currentProgram.getListing();
        int[] types = {
            CodeUnit.PLATE_COMMENT, CodeUnit.PRE_COMMENT, CodeUnit.POST_COMMENT,
            CodeUnit.EOL_COMMENT, CodeUnit.REPEATABLE_COMMENT
        };
        String[] names = { "plate", "pre", "post", "eol", "repeatable" };
        try (BufferedWriter out = writer(output)) {
            AddressIterator addresses = listing.getCommentAddressIterator(
                currentProgram.getMemory(), true);
            while (addresses.hasNext()) {
                monitor.checkCancelled();
                Address address = addresses.next();
                Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
                for (int index = 0; index < types.length; index++) {
                    String comment = listing.getComment(types[index], address);
                    if (comment == null || comment.isBlank()) continue;
                    records.add(new CommentRecord(address,
                        function == null ? "" : function.getEntryPoint().toString(false, false),
                        names[index], index, comment));
                }
            }
            // Function comments are the persistent decompiler-comment channel exposed by
            // Ghidra.  They are independent of listing comments and appear with the
            // corresponding function in the decompiler view.
            for (Function function : allFunctions()) {
                monitor.checkCancelled();
                String comment = function.getComment();
                if (comment != null && !comment.isBlank()) {
                    records.add(new CommentRecord(function.getEntryPoint(),
                        function.getEntryPoint().toString(false, false), "decompiler", 5, comment));
                }
            }
            Collections.sort(records);
            for (CommentRecord record : records) {
                out.write("{" + field("address_space", record.address.getAddressSpace().getName()) + "," +
                    field("address", record.address.toString(false, false)) + "," +
                    field("function_address", record.functionAddress) + "," +
                    field("type", record.type) + "," +
                    field("text", record.text) +
                    "}");
                out.newLine();
            }
        }
        counts.put("comments", (long) records.size());
    }

    private void exportCalls(File output, Map<String, Long> counts) throws Exception {
        long count = 0;
        try (BufferedWriter out = writer(output)) {
            for (Function caller : allFunctions()) {
                monitor.checkCancelled();
                Set<Function> called = caller.getCalledFunctions(monitor);
                List<Function> callees = new ArrayList<>(called);
                callees.sort(Comparator.comparing(Function::getEntryPoint));
                for (Function callee : callees) {
                    out.write("{" +
                        field("caller", caller.getEntryPoint().toString(false, false)) + "," +
                        field("caller_address_space", caller.getEntryPoint().getAddressSpace().getName()) + "," +
                        field("callee", callee.getEntryPoint().toString(false, false)) +
                        "," + field("callee_address_space", callee.getEntryPoint().getAddressSpace().getName()) +
                        "}");
                    out.newLine();
                    count++;
                }
            }
        }
        counts.put("calls", count);
    }

    private String externalLibraryName(Function function) {
        try {
            Object manager = currentProgram.getExternalManager();
            Method getLocation = manager.getClass().getMethod("getExternalLocation", Address.class);
            Object location = getLocation.invoke(manager, function.getEntryPoint());
            if (location == null) return "unknown";
            Method getLibraryName = location.getClass().getMethod("getLibraryName");
            Object library = getLibraryName.invoke(location);
            return library == null ? "unknown" : String.valueOf(library);
        }
        catch (ReflectiveOperationException exception) {
            // Preserve the import record even when an analyzer did not associate a DLL.
            return "unknown";
        }
    }

    private void exportImports(File output, Map<String, Long> counts) throws Exception {
        long count = 0;
        try (BufferedWriter out = writer(output)) {
            for (Function function : allFunctions()) {
                if (!function.isExternal()) continue;
                out.write("{" +
                    field("address_space", function.getEntryPoint().getAddressSpace().getName()) + "," +
                    field("address", function.getEntryPoint().toString(false, false)) + "," +
                    field("name", function.getName()) + "," +
                    field("signature", function.getSignature().getPrototypeString()) + "," +
                    field("library", externalLibraryName(function)) +
                    "}");
                out.newLine();
                count++;
            }
        }
        counts.put("imports", count);
    }

    private void exportExports(File output, Map<String, Long> counts) throws Exception {
        // Ghidra's generic symbol API does not retain PE ordinal information.  Read the
        // PE export directory directly so an unavailable symbol-table API cannot turn a
        // discovery failure into a misleading empty exports.jsonl.
        List<PeExport> exports = readPeExports(new File(currentProgram.getExecutablePath()));
        long count = 0;
        long previousRva = -1;
        try (BufferedWriter out = writer(output)) {
            for (PeExport export : exports) {
                // PE files may expose aliases with distinct names/ordinals for the same
                // entry point.  The structured contract is address-keyed, so retain one
                // deterministic representative (a named alias when one exists).
                if (export.rva == previousRva) continue;
                previousRva = export.rva;
                Address address = currentProgram.getImageBase().add(export.rva);
                Function function = currentProgram.getFunctionManager().getFunctionAt(address);
                String name = export.name.isEmpty() ? "Ordinal_" + export.ordinal : export.name;
                String signature = function == null || function.isExternal()
                    ? "" : function.getSignature().getPrototypeString();
                out.write("{" +
                    field("address_space", address.getAddressSpace().getName()) + "," +
                    field("address", address.toString(false, false)) + "," +
                    field("name", name) + "," +
                    field("signature", signature) + "," +
                    numberField("ordinal", export.ordinal) +
                    "}");
                out.newLine();
                count++;
            }
        }
        counts.put("exports", count);
    }

    private List<PeExport> readPeExports(File executable) throws Exception {
        if (!executable.isFile()) {
            throw new IllegalStateException("Cannot enumerate PE exports: executable is unavailable: " + executable);
        }
        try (RandomAccessFile input = new RandomAccessFile(executable, "r")) {
            if (readU16(input, 0) != 0x5a4d) throw new IllegalStateException("Executable is not a PE file: " + executable);
            long peOffset = readU32(input, 0x3c);
            if (readU32(input, peOffset) != 0x00004550L) throw new IllegalStateException("PE header is missing: " + executable);
            int sections = readU16(input, peOffset + 6);
            long optional = peOffset + 24;
            int magic = readU16(input, optional);
            if (magic != 0x20b && magic != 0x10b) throw new IllegalStateException("Unsupported PE optional-header magic");
            int optionalSize = readU16(input, peOffset + 20);
            long directory = optional + (magic == 0x20b ? 112 : 96);
            long exportRva = readU32(input, directory);
            long exportSize = readU32(input, directory + 4);
            if (exportRva == 0 || exportSize == 0) return Collections.emptyList();
            long sectionTable = optional + optionalSize;
            long exportOffset = rvaToOffset(input, exportRva, sections, sectionTable);
            long ordinalBase = readU32(input, exportOffset + 16);
            long functionCount = readU32(input, exportOffset + 20);
            long nameCount = readU32(input, exportOffset + 24);
            long functionsRva = readU32(input, exportOffset + 28);
            long namesRva = readU32(input, exportOffset + 32);
            long ordinalsRva = readU32(input, exportOffset + 36);
            if (functionCount > 1_000_000L || nameCount > 1_000_000L) {
                throw new IllegalStateException("Unreasonable PE export-table size");
            }
            Map<Long, String> names = new HashMap<>();
            for (long index = 0; index < nameCount; index++) {
                long nameRva = readU32(input, rvaToOffset(input, namesRva + index * 4, sections, sectionTable));
                long ordinalIndex = readU16(input, rvaToOffset(input, ordinalsRva + index * 2, sections, sectionTable));
                if (ordinalIndex >= functionCount) throw new IllegalStateException("PE export name has invalid ordinal index");
                names.put(ordinalIndex, readAsciiZ(input, rvaToOffset(input, nameRva, sections, sectionTable)));
            }
            List<PeExport> result = new ArrayList<>();
            for (long index = 0; index < functionCount; index++) {
                long functionRva = readU32(input, rvaToOffset(input, functionsRva + index * 4, sections, sectionTable));
                if (functionRva == 0) continue;
                // Forwarders have no SC6 code address, so are not local entry points.
                if (functionRva >= exportRva && functionRva < exportRva + exportSize) continue;
                result.add(new PeExport(functionRva, ordinalBase + index, names.getOrDefault(index, "")));
            }
            result.sort((left, right) -> {
                int compare = Long.compare(left.rva, right.rva);
                if (compare != 0) return compare;
                boolean leftNamed = !left.name.isEmpty();
                boolean rightNamed = !right.name.isEmpty();
                if (leftNamed != rightNamed) return leftNamed ? -1 : 1;
                return Long.compare(left.ordinal, right.ordinal);
            });
            return result;
        }
    }

    private long rvaToOffset(RandomAccessFile input, long rva, int sections, long sectionTable) throws Exception {
        for (int index = 0; index < sections; index++) {
            long section = sectionTable + index * 40L;
            long virtualSize = readU32(input, section + 8);
            long virtualAddress = readU32(input, section + 12);
            long rawSize = readU32(input, section + 16);
            long rawOffset = readU32(input, section + 20);
            long size = Math.max(virtualSize, rawSize);
            if (rva >= virtualAddress && rva - virtualAddress < size) {
                long offset = rawOffset + rva - virtualAddress;
                if (offset >= input.length()) throw new IllegalStateException("PE RVA maps beyond end of file");
                return offset;
            }
        }
        throw new IllegalStateException("PE RVA is not mapped by a section: 0x" + Long.toHexString(rva));
    }

    private int readU16(RandomAccessFile input, long offset) throws Exception {
        input.seek(offset);
        return Short.toUnsignedInt(Short.reverseBytes(input.readShort()));
    }

    private long readU32(RandomAccessFile input, long offset) throws Exception {
        input.seek(offset);
        return Integer.toUnsignedLong(Integer.reverseBytes(input.readInt()));
    }

    private String readAsciiZ(RandomAccessFile input, long offset) throws Exception {
        input.seek(offset);
        StringBuilder value = new StringBuilder();
        for (int index = 0; index < 4096; index++) {
            int character = input.readUnsignedByte();
            if (character == 0) return value.toString();
            value.append((char) character);
        }
        throw new IllegalStateException("Unterminated PE export name");
    }

    private List<Function> allFunctions() {
        Map<String, Function> byAddress = new LinkedHashMap<>();
        FunctionIterator internal = currentProgram.getFunctionManager().getFunctions(true);
        while (internal.hasNext()) {
            Function function = internal.next();
            byAddress.put(function.getEntryPoint().toString(true), function);
        }
        FunctionIterator external = currentProgram.getFunctionManager().getExternalFunctions();
        while (external.hasNext()) {
            Function function = external.next();
            byAddress.put(function.getEntryPoint().toString(true), function);
        }
        List<Function> functions = new ArrayList<>(byAddress.values());
        functions.sort((left, right) -> {
            int space = left.getEntryPoint().getAddressSpace().getName()
                .compareTo(right.getEntryPoint().getAddressSpace().getName());
            return space != 0 ? space : left.getEntryPoint().compareTo(right.getEntryPoint());
        });
        return functions;
    }

    private String decompilerOptionsJson(DecompileOptions options, int timeoutSeconds) {
        int retryTimeout = Math.max(20, timeoutSeconds * 4);
        return "{" +
            field("proto_eval_model", options.getProtoEvalModel()) + "," +
            field("function_brace_format", options.getFunctionBraceFormat().toString()) + "," +
            field("if_else_brace_format", options.getIfElseBraceFormat().toString()) + "," +
            field("loop_brace_format", options.getLoopBraceFormat().toString()) + "," +
            field("switch_brace_format", options.getSwitchBraceFormat().toString()) + "," +
            numberField("max_width", options.getMaxWidth()) + "," +
            field("keyword_color", String.valueOf(options.getKeywordColor())) + "," +
            field("type_color", String.valueOf(options.getTypeColor())) + "," +
            field("comment_color", String.valueOf(options.getCommentColor())) + "," +
            field("constant_color", String.valueOf(options.getConstantColor())) + "," +
            field("variable_color", String.valueOf(options.getVariableColor())) + "," +
            field("parameter_color", String.valueOf(options.getParameterColor())) + "," +
            field("global_color", String.valueOf(options.getGlobalColor())) + "," +
            field("special_color", String.valueOf(options.getSpecialColor())) + "," +
            field("default_color", String.valueOf(options.getDefaultColor())) + "," +
            field("error_color", String.valueOf(options.getErrorColor())) + "," +
            field("background_color", String.valueOf(options.getBackgroundColor())) + "," +
            field("current_variable_highlight_color", String.valueOf(options.getCurrentVariableHighlightColor())) + "," +
            field("middle_mouse_highlight_color", String.valueOf(options.getMiddleMouseHighlightColor())) + "," +
            field("search_highlight_color", String.valueOf(options.getSearchHighlightColor())) + "," +
            numberField("middle_mouse_highlight_button", options.getMiddleMouseHighlightButton()) + "," +
            boolField("include_pre_comments", options.isPRECommentIncluded()) + "," +
            boolField("include_plate_comments", options.isPLATECommentIncluded()) + "," +
            boolField("include_post_comments", options.isPOSTCommentIncluded()) + "," +
            boolField("include_eol_comments", options.isEOLCommentIncluded()) + "," +
            boolField("include_warn_comments", options.isWARNCommentIncluded()) + "," +
            boolField("include_head_comments", options.isHeadCommentIncluded()) + "," +
            boolField("eliminate_unreachable", options.isEliminateUnreachable()) + "," +
            boolField("respect_read_only", options.isRespectReadOnly()) + "," +
            boolField("simplify_double_precision", options.isSimplifyDoublePrecision()) + "," +
            boolField("display_line_numbers", options.isDisplayLineNumbers()) + "," +
            field("display_language", options.getDisplayLanguage().toString()) + "," +
            field("name_transformer", nameTransformerDescriptor(options)) + "," +
            boolField("convention_print", options.isConventionPrint()) + "," +
            boolField("no_cast_print", options.isNoCastPrint()) + "," +
            field("default_font", String.valueOf(options.getDefaultFont())) + "," +
            numberField("default_timeout_seconds", options.getDefaultTimeout()) + "," +
            numberField("max_payload_megabytes", options.getMaxPayloadMBytes()) + "," +
            numberField("max_instructions", options.getMaxInstructions()) + "," +
            numberField("max_jump_table_entries", options.getMaxJumpTableEntries()) + "," +
            field("comment_style", options.getCommentStyle().toString()) + "," +
            numberField("cache_size", options.getCacheSize()) + "," +
            numberField("parallel_chunk_size", DECOMPILE_CHUNK_SIZE) + "," +
            numberField("retry_timeout_seconds", retryTimeout) + "," +
            field("retryable_statuses", "timeout,decompile-error") +
            "}";
    }

    private String nameTransformerDescriptor(DecompileOptions options) {
        Object transformer = options.getNameTransformer();
        if (transformer == null) return "";
        // grabFromProgram() leaves the default IdentityNameTransformer stateless, but
        // Object.toString() adds a per-run identity hash. If this exporter ever starts
        // accepting configured/custom transformers, serialize stable configuration here
        // (or reject it) rather than silently treating distinct settings as equivalent.
        return transformer.getClass().getName();
    }

    private void exportInfo(File output, DecompileOptions options, int timeoutSeconds,
            Map<String, Long> counts) throws Exception {
        StringBuilder countJson = new StringBuilder();
        boolean first = true;
        for (Map.Entry<String, Long> entry : counts.entrySet()) {
            if (!first) countJson.append(',');
            first = false;
            countJson.append(json(entry.getKey())).append(':').append(entry.getValue());
        }
        String executableSha = nullToEmpty(currentProgram.getExecutableSHA256());
        String json = "{" +
            field("schema", SCHEMA) + "," +
            field("program_name", currentProgram.getName()) + "," +
            field("program_path", currentProgram.getDomainFile().getPathname()) + "," +
            field("executable_path", nullToEmpty(currentProgram.getExecutablePath())) + "," +
            field("executable_sha256", executableSha) + "," +
            field("image_base", currentProgram.getImageBase().toString(false, false)) + "," +
            field("language", currentProgram.getLanguageID().getIdAsString()) + "," +
            field("compiler_spec", currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString()) + "," +
            field("ghidra_version", Application.getApplicationVersion()) + "," +
            numberField("modification_number", startModificationNumber) + "," +
            numberField("timeout_seconds", timeoutSeconds) + "," +
            "\"decompiler_options\":" + decompilerOptionsJson(options, timeoutSeconds) + "," +
            "\"counts\":{" + countJson + "}" +
            "}";
        try (BufferedWriter writer = writer(output)) {
            writer.write(json);
            writer.newLine();
        }
    }

    private static String typeKind(DataType type) {
        if (type instanceof ghidra.program.model.data.Structure) return "struct";
        if (type instanceof ghidra.program.model.data.Union) return "union";
        if (type instanceof Enum) return "enum";
        if (type instanceof TypeDef) return "typedef";
        if (type instanceof Pointer) return "pointer";
        if (type instanceof Array) return "array";
        return "type";
    }

    private static BufferedWriter writer(File file) throws Exception {
        File parent = file.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IllegalStateException("Unable to create directory: " + parent);
        }
        return new BufferedWriter(new OutputStreamWriter(
            new FileOutputStream(file), StandardCharsets.UTF_8), 1024 * 1024);
    }

    private static void ensureWriterSucceeded(File file) {
        if (!file.isFile()) {
            throw new IllegalStateException("Expected export file was not created: " + file);
        }
    }

    private static String field(String name, String value) {
        return json(name) + ":" + json(nullToEmpty(value));
    }

    private static String boolField(String name, boolean value) {
        return json(name) + ":" + (value ? "true" : "false");
    }

    private static String numberField(String name, long value) {
        return json(name) + ":" + value;
    }

    private static String arrayField(String name, List<String> values) {
        StringBuilder result = new StringBuilder(json(name)).append(": [");
        for (int index = 0; index < values.size(); index++) {
            if (index > 0) result.append(',');
            result.append(json(values.get(index)));
        }
        return result.append(']').toString();
    }

    private static String json(String value) {
        if (value == null) return "\"\"";
        StringBuilder out = new StringBuilder(value.length() + 16).append('"');
        for (int index = 0; index < value.length(); index++) {
            char ch = value.charAt(index);
            switch (ch) {
                case '"': out.append("\\\""); break;
                case '\\': out.append("\\\\"); break;
                case '\b': out.append("\\b"); break;
                case '\f': out.append("\\f"); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                default:
                    if (ch < 0x20) out.append(String.format("\\u%04x", (int) ch));
                    else out.append(ch);
            }
        }
        return out.append('"').toString();
    }

    private static String sha256(byte[] bytes) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] hash = digest.digest(bytes);
        StringBuilder result = new StringBuilder(hash.length * 2);
        for (byte value : hash) result.append(String.format("%02x", value & 0xff));
        return result.toString();
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private static String safeMessage(Exception exc) {
        return exc.getMessage() == null ? "" : exc.getMessage();
    }
}
