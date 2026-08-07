using CUE4Parse.FileProvider;
using CUE4Parse.UE4.Assets.Exports.SkeletalMesh;
using CUE4Parse.UE4.Versions;
using Newtonsoft.Json;

if (args.Length < 2)
{
    Console.Error.WriteLine("usage: asset_property_dump <asset-root> <relative-uasset> [relative-uasset ...]");
    return 2;
}

var provider = new DefaultFileProvider(
    Path.GetFullPath(args[0]),
    SearchOption.AllDirectories,
    true,
    new VersionContainer(EGame.GAME_UE4_17));
provider.Initialize();
provider.PostMount();

for (var i = 1; i < args.Length; ++i)
{
    var assetPath = args[i].Replace('\\', '/');
    var package = provider.LoadPackage(assetPath);
    var exports = package.GetExports();
    var payload = new
    {
        asset = assetPath,
        exports,
        skeletalMeshes = exports.OfType<USkeletalMesh>().Select(mesh => new
        {
            mesh = mesh.Name,
            bones = mesh.ReferenceSkeleton.FinalRefBoneInfo.Select((bone, boneIndex) => new
            {
                index = boneIndex,
                name = bone.Name.Text,
                parentIndex = bone.ParentIndex,
                rotation = mesh.ReferenceSkeleton.FinalRefBonePose[boneIndex].Rotation,
                translation = mesh.ReferenceSkeleton.FinalRefBonePose[boneIndex].Translation,
                scale = mesh.ReferenceSkeleton.FinalRefBonePose[boneIndex].Scale3D,
            })
        })
    };
    Console.WriteLine(JsonConvert.SerializeObject(payload, Formatting.Indented));
}

return 0;
