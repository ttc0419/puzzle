#pragma once

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"

#include "Interactable.h"

#include "RemoteSwitch.generated.h"

DECLARE_EVENT(ARemoteSwitch, FOnSwitchToggle);

UCLASS()
class ASSIGNMENT_1_API ARemoteSwitch : public AActor, public IInteractable
{
	GENERATED_BODY()
	
private:
	bool bOpened;

	UMaterial *OnMaterial;
	UMaterial *OffMaterial;
	UStaticMeshComponent *SwitchMeshComp;

public:
	ARemoteSwitch();
	void BeginPlay() override;

	FOnSwitchToggle OnSwitchToggle;

	void Interact() override;
};