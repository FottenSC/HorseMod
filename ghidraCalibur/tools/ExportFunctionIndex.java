// Writes function metadata from the current Ghidra program without exporting decompiled bodies.

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportFunctionIndex extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			throw new IllegalArgumentException("Usage: ExportFunctionIndex.java <output-csv-path>");
		}

		File outputFile = new File(args[0]);
		File parent = outputFile.getParentFile();
		if (parent != null) {
			parent.mkdirs();
		}

		println("Function index input program: " + currentProgram.getName());
		println("Function index output file: " + outputFile.getAbsolutePath());

		try (PrintWriter writer = new PrintWriter(outputFile, "UTF-8")) {
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

		println("Function index finished.");
	}

	private String csv(String value) {
		if (value == null) {
			return "";
		}
		return "\"" + value.replace("\"", "\"\"") + "\"";
	}
}
