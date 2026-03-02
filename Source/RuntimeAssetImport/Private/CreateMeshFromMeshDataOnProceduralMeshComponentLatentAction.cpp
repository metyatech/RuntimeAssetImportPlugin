// Copyright (c) 2026 metyatech. All rights reserved.

#include "CreateMeshFromMeshDataOnProceduralMeshComponentLatentAction.h"

#include "AssetConstructorHelpers.h"

FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction::
    FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction(
        const FLatentActionInfo&  InLatentInfo,
        const FLoadedMeshData&    InMeshData,
        UMaterialInterface&       InOutParentMaterialInterface,
        UProceduralMeshComponent& InOutTargetProceduralMeshComponent)
    : ExecutionFunction(InLatentInfo.ExecutionFunction),
      OutputLink(InLatentInfo.Linkage),
      CallbackTarget(InLatentInfo.CallbackTarget) {
	namespace Tasks = UE::Tasks;

	// check that the NodeList in InMeshData has at least one node
	// (because there must be a root node)
	check(!InMeshData.NodeList.IsEmpty());

	// change state to running
	IsRunning = true;

	// get node list
	const auto& NodeList = InMeshData.NodeList;

	// number of the NodeList
	const auto& NumNodeList = NodeList.Num();

	// get material list
	const auto& MaterialList = InMeshData.MaterialList;

	// generate material instances
	auto MaterialInstances =
	    GenerateMaterialInstances(InOutTargetProceduralMeshComponent,
	                              MaterialList, InOutParentMaterialInterface);

	// index of a mesh section in InOutTargetProceduralMeshComponent
	int32 MeshSectionIndex = 0;

	// Tasks to calculate transform matrix relative to the origin of
	// InOutTargetProceduralMeshComponent
	TArray<Tasks::TTask<FTransform>> CalcTFMatrixTasks;
	CalcTFMatrixTasks.AddDefaulted(NumNodeList);

	// Task list of CreateMeshSectionTask
	TArray<Tasks::FTask> CreateMeshSectionTasks;
	CreateMeshSectionTasks.Reserve(NumNodeList);

	// for all node in NodeList
	for (auto Node_i = decltype(NumNodeList){0}; Node_i < NumNodeList; ++Node_i) {
		// get reference of the node
		const auto& Node = NodeList[Node_i];

		// get RelativeTransform
		const auto& RelativeTransform = Node.RelativeTransform;

		// Task to create calculate transform, invoked after
		// ParentCalcTFTask is completed
		auto ParentCalcTFTask = [&]() {
			// if this node is a root node
			if (0 == Node_i) {
				// return a Task that only return identity transform matrix
				return Tasks::MakeCompletedTask<FTransform>(FTransform::Identity);
			}

			// if this node is a non-root node

			// get parent node index
			const auto& ParentNodeIndex = Node.ParentNodeIndex;

			// get calculate transform matrix task of parent of this node
			auto& ParentCalcTFTask = CalcTFMatrixTasks[ParentNodeIndex];

			// return parent's transform relative to target.
			return ParentCalcTFTask;
		}();

		// Task to create calculate transform, invoked after
		// ParentCalcTFTask is completed
		auto CalcTFTask = CalcTFMatrixTasks[Node_i] = Tasks::Launch(
		    UE_SOURCE_LOCATION,
		    [ParentCalcTFTask, RelativeTransform]() mutable {
			    // ParentCalcTFTask should be completed
			    check(ParentCalcTFTask.IsCompleted());

			    // get parent's transform relative to target
			    const auto& ParentTransformToTarget = ParentCalcTFTask.GetResult();

			    // calculate transform relative to target
			    // To convert local coordinates to world coordinates:
			    //   Assume the following parent-child relationship
			    //     Child1 (parent) - Child2 (child) - Child3
			    //     (grandchild)
			    //
			    //   Let
			    //    - DVn (Translation) be the relative coordinate vector
			    //    of
			    //      Child n to its parent
			    //    - Rn (Rotation) be the relative rotation vector of
			    //    ...
			    //    - Sn (Scale) be the relative scale vector of ...
			    //   and represent them in a single matrix:
			    //    - "Transform(n)" be the transformation matrix
			    //      (If n is just a number, I omit the parentheses)
			    //
			    //   then, let the absolute coordinate vector Vn be,
			    //   V3 = S1*R1*(S2*R2*DV3 + DV2) + DV1
			    //      = S1*R1*(Transform2*DV3) + DV1
			    //      = Transform1*(Transform2*DV3)
			    //      = (Transform1 * Transform2) * DV3
			    //
			    // So the transformation matrix that transforms relative
			    // coordinate of Child n to absolute coordinate can be
			    // calculated by
			    //    Transform1 * Transform2 * ... * Transform(n-1) ......
			    //    <1>
			    //
			    // However, in the Unreal Engine, the transformation matrix
			    // is held in the form of a transposed matrix. For any
			    // matrix A, let t(A) be its transpose matrix, then the
			    // formula <1> equals
			    //    t(t(Transform(n-1)) * ... * t(Transform2) *
			    //    t(Transform1))
			    //
			    // Therefore, I calculate as follows:
			    const auto& TransformToTarget =
			        FTransform(RelativeTransform.ToMatrixWithScale() *
			                   ParentTransformToTarget.ToMatrixWithScale());

			    return TransformToTarget;
		    },
		    ParentCalcTFTask, LowLevelTasks::ETaskPriority::BackgroundNormal);

		// get reference of the sections
		const auto& Sections = Node.Sections;

		// get number of sections
		const auto& NumSections = Sections.Num();

		// create mesh sections
		for (auto Section_i = decltype(NumSections){0}; Section_i < NumSections;
		     ++Section_i) {
			// get reference of the section
			const auto& Section = Sections[Section_i];

			// get Vertices relative to my parent node
			const auto& Vertices = Section.Vertices;

			// get Normals relative to my parent node
			const auto& Normals = Section.Normals;

			// get Tangents relative to my parent node
			const auto& Tangents = Section.Tangents;

			// Task to calculate vertices relative to target, invoked after CalcTFTask
			// is completed.
			auto CalcVerticesRelativeToTargetTask = Tasks::Launch(
			    UE_SOURCE_LOCATION,
			    [CalcTFTask, Vertices]() mutable {
				    // CalcTFTask should be completed
				    check(CalcTFTask.IsCompleted());

				    // get TransformToTarget
				    const auto& TransformToTarget = CalcTFTask.GetResult();

				    // generate Vertices relative to
				    // InOutTargetProceduralMeshComponent
				    auto VerticesRelativeToTarget = decltype(Vertices){};
				    Algo::Transform(Vertices, VerticesRelativeToTarget,
				                    [&TransformToTarget](const FVector& vector) {
					                    return TransformToTarget.TransformFVector4(
					                        vector);
				                    });
				    return VerticesRelativeToTarget;
			    },
			    CalcTFTask, LowLevelTasks::ETaskPriority::BackgroundNormal);

			// Task to calculate normals relative to target, invoked after CalcTFTask
			// is completed.
			auto CalcNormalsRelativeToTargetTask = Tasks::Launch(
			    UE_SOURCE_LOCATION,
			    [CalcTFTask, Normals]() mutable {
				    // CalcTFTask should be completed
				    check(CalcTFTask.IsCompleted());

				    // get TransformToTarget
				    const auto& TransformToTarget = CalcTFTask.GetResult();

				    // rotator of TransformToTarget
				    const auto& TransformToTargetRotator = TransformToTarget.Rotator();

				    // generate Normals relative to
				    // InOutTargetProceduralMeshComponent
				    auto NormalsRelativeToTarget = decltype(Normals){};
				    Algo::Transform(Normals, NormalsRelativeToTarget,
				                    [&TransformToTargetRotator](const FVector& vector) {
					                    return TransformToTargetRotator.RotateVector(
					                        vector);
				                    });
				    return NormalsRelativeToTarget;
			    },
			    CalcTFTask, LowLevelTasks::ETaskPriority::BackgroundNormal);

			// Task to calculate tangets relative to target, invoked after CalcTFTask
			// is completed.
			auto CalcTangentsRelativeToTargetTask = Tasks::Launch(
			    UE_SOURCE_LOCATION,
			    [CalcTFTask, Tangents]() mutable {
				    // CalcTFTask should be completed
				    check(CalcTFTask.IsCompleted());

				    // get TransformToTarget
				    const auto& TransformToTarget = CalcTFTask.GetResult();

				    // rotator of TransformToTarget
				    const auto& TransformToTargetRotator = TransformToTarget.Rotator();

				    // generate Tangents relative to
				    // InOutTargetProceduralMeshComponent
				    auto TangentsRelativeToTarget = decltype(Tangents){};
				    Algo::Transform(Tangents, TangentsRelativeToTarget,
				                    [&TransformToTargetRotator](
				                        const FProcMeshTangent& ProcMeshTangent) {
					                    return FProcMeshTangent(
					                        TransformToTargetRotator.RotateVector(
					                            ProcMeshTangent.TangentX),
					                        ProcMeshTangent.bFlipTangentY);
				                    });
				    return TangentsRelativeToTarget;
			    },
			    CalcTFTask, LowLevelTasks::ETaskPriority::BackgroundNormal);

			// Task to create mesh section on game thread, invoked after
			// CalcVerticesRelativeToTargetTask, CalcNormalsRelativeToTargetTask
			// and CalcTangentsRelativeToTargetTask are completed.
			auto CreateMeshSectionTask_GameThread = Tasks::Launch(
			    UE_SOURCE_LOCATION,
			    [=, &InOutTargetProceduralMeshComponent]() mutable {
				    ExecuteOnGameThread(
				        UE_SOURCE_LOCATION,
				        [=, &InOutTargetProceduralMeshComponent]() mutable {
					        // CalcVerticesRelativeToTargetTask should be completed
					        check(CalcVerticesRelativeToTargetTask.IsCompleted());
					        // CalcNormalsRelativeToTargetTask should be completed
					        check(CalcNormalsRelativeToTargetTask.IsCompleted());
					        // CalcTangentsRelativeToTargetTask should be completed
					        check(CalcTangentsRelativeToTargetTask.IsCompleted());

					        // get Vertices relative to target
					        const auto& VerticesRelativeToTarget =
					            CalcVerticesRelativeToTargetTask.GetResult();

					        // get Normals relative to target
					        const auto& NormalsRelativeToTarget =
					            CalcNormalsRelativeToTargetTask.GetResult();

					        // get Tangents relative to target
					        const auto& TangentsRelativeToTarget =
					            CalcTangentsRelativeToTargetTask.GetResult();

					        // create mesh section
					        InOutTargetProceduralMeshComponent
					            .CreateMeshSection_LinearColor(
					                MeshSectionIndex, VerticesRelativeToTarget,
					                Section.Triangles, NormalsRelativeToTarget,
					                Section.UV0Channel, Section.VertexColors0,
					                TangentsRelativeToTarget,
					                /* CreateCollision = */ true,
					                /* bSRGBConversion = */ false);
				        });
			    },
			    Tasks::Prerequisites(CalcVerticesRelativeToTargetTask,
			                         CalcNormalsRelativeToTargetTask,
			                         CalcTangentsRelativeToTargetTask),
			    LowLevelTasks::ETaskPriority::Normal);

			// add CreateMeshSectionTask
			CreateMeshSectionTasks.Add(MoveTemp(CreateMeshSectionTask_GameThread));

			// set Material
			const auto& MaterialIndex = Section.MaterialIndex;

			// Task to set material instance on game thread, invoked after
			// GenerateMaterialInstancesTask is completed.
			const auto& MaterialInstance = MaterialInstances[MaterialIndex];

			// set Material
			InOutTargetProceduralMeshComponent.SetMaterial(MeshSectionIndex,
			                                               MaterialInstance);

			// increment index of a mesh section in
			// InOutTargetProceduralMeshComponent
			MeshSectionIndex++;
		}
	}

	// Task when all create mesh section completed.
	Tasks::Launch(
	    UE_SOURCE_LOCATION,
	    [&]() {
		    // Put latent node into completion state
		    IsRunning = false;
	    },
	    CreateMeshSectionTasks, LowLevelTasks::ETaskPriority::Normal);
}

void FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction::
    UpdateOperation(FLatentResponse& Response) {
	Response.FinishAndTriggerIf(IsRunning == false, ExecutionFunction, OutputLink,
	                            CallbackTarget);
}

void FCreateMeshFromMeshDataOnProceduralMeshComponentLatentAction::Finish() {
	IsRunning = false;
}
