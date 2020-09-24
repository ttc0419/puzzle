#pragma once

#include "AIController.h"
#include "GameFramework/Character.h"

#include "AICharacter.generated.h"

UCLASS()
class ASSIGNMENT_API AAICharacter final : public ACharacter
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere)
	TSubclassOf<AAIController> AIControllerClassOverride;

public:
	AAICharacter();
	void BeginPlay() override;

	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent *HitComponent,
		AActor *OtherActor, UPrimitiveComponent *OtherComp, FVector NormalImpulse, const FHitResult &Hit);
};
