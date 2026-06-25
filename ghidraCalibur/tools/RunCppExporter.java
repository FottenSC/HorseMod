// Runs Ghidra's built-in C/C++ exporter from analyzeHeadless.

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileOptions.CommentStyleEnum;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.exporter.CppExporter;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class RunCppExporter extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			throw new IllegalArgumentException("Usage: RunCppExporter.java <output-cpp-path>");
		}

		File outputFile = new File(args[0]);
		File parent = outputFile.getParentFile();
		if (parent != null) {
			parent.mkdirs();
		}

		int timeoutSeconds = 5;
		if (args.length >= 2) {
			timeoutSeconds = Integer.parseInt(args[1]);
		}
		File functionIndexFile = null;
		if (args.length >= 3) {
			functionIndexFile = new File(args[2]);
		}

		DecompileOptions options = new DecompileOptions();
		options.setCommentStyle(CommentStyleEnum.CPPStyle);
		options.setDefaultTimeout(timeoutSeconds);

		if (functionIndexFile != null) {
			writeFunctionIndex(functionIndexFile);
		}

		CppExporter exporter = new CppExporter(
			options,
			true,  // create header
			true,  // create C/C++ file
			true,  // emit data-type definitions
			true,  // emit referenced globals
			true,  // exclude matching tags when tag list is non-empty
			""     // no tag filter
		);

		println("CppExporter input program: " + currentProgram.getName());
		println("CppExporter output file: " + outputFile.getAbsolutePath());
		println("CppExporter per-function timeout: " + timeoutSeconds + " seconds");

		boolean success = exporter.export(outputFile, currentProgram, currentProgram.getMemory(), monitor);
		println(exporter.getMessageLog().toString());

		if (!success) {
			throw new RuntimeException("CppExporter returned false; check the Ghidra log for details.");
		}

		println("CppExporter finished.");
	}

	private void writeFunctionIndex(File functionIndexFile) throws Exception {
		File parent = functionIndexFile.getParentFile();
		if (parent != null) {
			parent.mkdirs();
		}
		println("Writing function index: " + functionIndexFile.getAbsolutePath());
		try (PrintWriter writer = new PrintWriter(functionIndexFile, "UTF-8")) {
			writer.println("address,name,namespace,signature,is_thunk,is_external");
			FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
			while (iterator.hasNext() && !monitor.isCancelled()) {
				Function function = iterator.next();
				writer.print(csv(function.getEntryPoint().toString()));
				writer.print(",");
				writer.print(csv(function.getName()));
				writer.print(",");
				writer.print(csv(function.getParentNamespace().getName(true)));
				writer.print(",");
				writer.print(csv(function.getSignature().getPrototypeString()));
				writer.print(",");
				writer.print(function.isThunk());
				writer.print(",");
				writer.println(function.isExternal());
			}
		}
	}

	private String csv(String value) {
		if (value == null) {
			return "";
		}
		return "\"" + value.replace("\"", "\"\"") + "\"";
	}
}
