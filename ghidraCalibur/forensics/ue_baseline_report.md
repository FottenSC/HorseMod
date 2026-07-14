# SC6 Unreal Engine Source-Baseline Forensics

## Conclusion

**inconclusive; evidence supports UE 4.17.2 but not a source lineage**

- staging-only matches: 0
- release-only matches: 2
- required independently corroborated matches: 3
- unconfirmed source/binary string matches: 2

A result below the threshold is deliberately inconclusive; it is not evidence for stock UE or staging lineage.

## Candidates

| Candidate | Commit | Build version |
| --- | --- | --- |
| final-4.17.2-release | `8c46d0805b8efa845cc693b76030b7cab2796c0a` | 4.17.2 |
| staging-4.17-pre-4.18 | `cf8cae2458ae798a9427416d9f38f0a1b2021f51` | 4.17.0 |
| staging-4.17-start | `96bd71478cba6500da916aef40d392a04b24302f` | 4.17.0 |

## Binary evidence

- `++UE4+Release-4.17`: ASCII=[55380032, 55380328, 62886393, 62888009, 62888521, 62889193, 62889465, 62890921], UTF-16LE=[55322880, 55322920]
- `4.17.2.0`: ASCII=[], UTF-16LE=[71025304]

## Discriminators

| Baseline-only marker | Source path | SC6 result |
| --- | --- | --- |
| staging-only: `Failed replicator RepNotifies check. Num=%d. Object=%s.` | `Engine/Source/Runtime/Engine/Private/DataReplication.cpp` | absent (unverified) |
| staging-only: `Failed RepState RepNotifies check. Num=%d. Object=%s` | `Engine/Source/Runtime/Engine/Private/DataReplication.cpp` | absent (unverified) |
| staging-only: `LoadLibraryWithSearchPaths failed for file %s. GetLastError=%d` | `Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformProcess.cpp` | absent (unverified) |
| staging-only: `UGameplayStatics::SpawnObject wrong class: %s` | `Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp` | absent (unverified) |
| staging-only: `FileExists returned %d for Module %s` | `Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformProcess.cpp` | absent (unverified) |
| staging-only: `SpawnObject wrong class: {0}'` | `Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp` | absent (unverified) |
| release-only: `Failed to initialize streaming wave data due to lack of serialized stream chunks. Error during stream cooking.` | `Engine/Source/Runtime/Engine/Private/AudioStreaming.cpp` | present (unverified) |
| release-only: `CancelAsyncLoadingInternal is not thread safe! This must be fixed before being enabled for EDL` | `Engine/Source/Runtime/CoreUObject/Private/Serialization/AsyncLoading.cpp` | present (confirmed) |
| release-only: `Go to end of the sequence and stop. Adheres to 'When Finished' section rules.` | `Engine/Source/Runtime/MovieScene/Public/MovieSceneSequencePlayer.h` | absent (unverified) |
| release-only: `Forcibly toggles the 'GPU Crashed' flag for testing crash analytics.` | `Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp` | present (confirmed) |
| release-only: `ResolveChannelsToData must be implemented to blend SourceData with multi-channel data.` | `Engine/Source/Runtime/MovieScene/Public/Evaluation/Blending/MovieSceneMultiChannelBlending.h` | absent (unverified) |
| release-only: `MultiChannelFromData must be implemented to blend SourceData with multi-channel data.` | `Engine/Source/Runtime/MovieScene/Public/Evaluation/Blending/MovieSceneMultiChannelBlending.h` | absent (unverified) |
| release-only: `Attempting to apply a compound data type with some channels uninitialized.` | `Engine/Source/Runtime/MovieScene/Public/Evaluation/Blending/MovieSceneMultiChannelBlending.h` | absent (unverified) |
| release-only: `Component Registered state changed from %s to %s within FRenderStateRecreator scope.` | `Engine/Source/Runtime/Engine/Classes/Components/SkinnedMeshComponent.h` | absent (unverified) |
| release-only: `Invalid minimum package file summary size (s.MaxPackageSummarySize=%d), %d is min.` | `Engine/Source/Runtime/CoreUObject/Private/Serialization/AsyncLoading.cpp` | absent (unverified) |
| release-only: `Debug command to dump the memory allocated by existing FArhiveAsync2.` | `Engine/Source/Runtime/CoreUObject/Private/Serialization/AsyncLoading.cpp` | absent (unverified) |
| release-only: `ConvertRenderTargetToTexture2DEditorOnly: render target has been released.` | `Engine/Source/Runtime/Engine/Private/KismetRenderingLibrary.cpp` | absent (unverified) |
| release-only: `Attempting to evaluate an Animation track with a null object.` | `Engine/Source/Runtime/MovieSceneTracks/Private/Evaluation/MovieSceneSkeletalAnimationTemplate.cpp` | absent (unverified) |
| release-only: `Attempting to evaluate a Transform track with a null object.` | `Engine/Source/Runtime/MovieSceneTracks/Private/Evaluation/MovieScene3DTransformTemplate.cpp` | absent (unverified) |
| release-only: `Attempting to evaluate a Color track with a null object.` | `Engine/Source/Runtime/MovieSceneTracks/Private/Evaluation/MovieSceneColorTemplate.cpp` | absent (unverified) |
| release-only: `Names must be less than {CharCount} characters long.` | `Engine/Source/Runtime/Engine/Private/ActorEditorUtils.cpp` | present (unverified) |

## Limits

- SC6 reports `CL-0`; version metadata cannot identify a private source commit.
- Source strings can be removed from Shipping builds. A source/binary string match is provisional until a Ghidra control-flow, layout, serializer, or reflection check confirms it; only confirmed markers affect the baseline score.
