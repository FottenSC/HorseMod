# Third-party tools

## BlueprintToCpp

`tools/BlueprintToCpp` is not authored by the HorseMod project. It is a Git
submodule derived from [Krowe-moh/BlueprintToCpp](https://github.com/Krowe-moh/BlueprintToCpp),
copyright Krowe Moh and distributed under the MIT License included in the
submodule as `LICENSE`.

HorseMod tracks the attribution-preserving GitHub fork
[FottenSC/BlueprintToCpp](https://github.com/FottenSC/BlueprintToCpp) on branch
`horsemod-sc6-support`. Commit `88015be` adds support for loose SC6 asset dumps
and the local SC6 analysis configuration. BlueprintToCpp output is reverse-
engineering evidence and is not compiled into HorseMod.

BlueprintToCpp depends on
[CUE4Parse](https://github.com/FabianFG/CUE4Parse), which is retained as the
upstream project's own submodule with its license and notice files intact.
