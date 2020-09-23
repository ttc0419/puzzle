#include "PlayerCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

#include "Interactable.h"

static const FVector AimOffset(0.0f, 64.0f, 90.0f);

APlayerCharacter::APlayerCharacter()
	: InteractableItem(nullptr), Health(1.0f), FirstAidKitNumber(0), KeyNumber(0), FuseNumber(0)
{
	PrimaryActorTick.bCanEverTick = true;

	/* Capsule Component */
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);
	GetCapsuleComponent()->SetNotifyRigidBodyCollision(false);
	GetCapsuleComponent()->OnComponentBeginOverlap.AddDynamic(this, &APlayerCharacter::OnCapsuleBeginOverlap);

	/* Set Skeletal Mesh */
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CharacterSKMeshAsset(TEXT("SkeletalMesh'/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin'"));
	GetMesh()->SetSkeletalMesh(CharacterSKMeshAsset.Object);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -94.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	/* Set Animation */
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnumBPClass(TEXT("AnimBlueprint'/Game/Mannequin/Animations/ThirdPerson_AnimBP.ThirdPerson_AnimBP_C'"));
	GetMesh()->SetAnimInstanceClass(AnumBPClass.Class);

	/* Movement */
	GetCharacterMovement()->MaxWalkSpeed = 500.0f;

	/* Spring Arm Component */
	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm Component"));
	SpringArmComp->SetupAttachment(RootComponent);

	SpringArmComp->SocketOffset = AimOffset;
	SpringArmComp->TargetArmLength = 256.0f;
	SpringArmComp->bUsePawnControlRotation = true;

	/* Camera Component */
	TPCameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("Third Person Camera Component"));
	TPCameraComp->SetupAttachment(SpringArmComp);

	static ConstructorHelpers::FObjectFinder<UMaterial> FogPPMAsset(TEXT("Material'/Game/Materials/PPM_Fog.PPM_Fog'"));
	TPCameraComp->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, FogPPMAsset.Object));

	/* Camera Auto Exposure */
	TPCameraComp->PostProcessSettings.bOverride_AutoExposureMethod = true;
	TPCameraComp->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	TPCameraComp->PostProcessSettings.bOverride_CameraISO = true;
	TPCameraComp->PostProcessSettings.CameraISO = 65536.0f;

	/* Camera Effects */
	TPCameraComp->PostProcessSettings.bOverride_VignetteIntensity = true;
	TPCameraComp->PostProcessSettings.VignetteIntensity = 0.75f;
	TPCameraComp->PostProcessSettings.bOverride_BloomIntensity = true;
	TPCameraComp->PostProcessSettings.BloomIntensity = 0.0f;
	TPCameraComp->PostProcessSettings.bOverride_SceneFringeIntensity = true;
	TPCameraComp->PostProcessSettings.SceneFringeIntensity = 1.0f;
	TPCameraComp->PostProcessSettings.bOverride_MotionBlurAmount = true;
	TPCameraComp->PostProcessSettings.MotionBlurAmount = 0.0f;

	/* Flash Light Component */
	FlashLightComp = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flash Light Component"));
	FlashLightComp->SetupAttachment(SpringArmComp);

	FlashLightComp->SetRelativeLocation(FVector(16.0f, 0.0f, 32.0f));
	FlashLightComp->Intensity = 16384.0f;
	FlashLightComp->AttenuationRadius = 8192.0f;
	FlashLightComp->InnerConeAngle = 16.0f;
	FlashLightComp->OuterConeAngle = 32.0f;

	/* Actor Damage Binding */
	OnTakeAnyDamage.AddDynamic(this, &APlayerCharacter::OnCharacterTakeDamage);

	/* Posses by Player */
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	/* Create HUD Widget */
	UClass *HUDWBPClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("WidgetBlueprint'/Game/UI/WBP_HUD.WBP_HUD_C'"));
	HUDWBP = CreateWidget<UHUDUserWidget>(GetWorld(), HUDWBPClass);
	HUDWBP->AddToViewport(0);
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TPCamTranform = TPCameraComp->GetComponentToWorld();
	TraceStart = TPCamTranform.GetLocation();
	TraceEnd = TraceStart + TPCamTranform.GetRotation().GetForwardVector() * 512.0f;

	if (GetWorld()->LineTraceSingleByChannel(LineTaceHitRes, TraceStart, TraceEnd, ECollisionChannel::ECC_Visibility)) {
		if (LineTaceHitRes.Actor.IsValid() && LineTaceHitRes.Actor->GetClass()->ImplementsInterface(UInteractable::StaticClass())) {
			InteractableItem = Cast<IInteractable>(LineTaceHitRes.Actor);
			InteractableItem->ItemWidgetComp->SetVisibility(true);
		} else if (InteractableItem != nullptr) {
			InteractableItem->ItemWidgetComp->SetVisibility(false);
			InteractableItem = nullptr;
		}
	}
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &APlayerCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &APlayerCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);

	PlayerInputComponent->BindAction("Pause", IE_Pressed, this, &APlayerCharacter::Pause);
	PlayerInputComponent->BindAction("Interact", IE_Pressed, this, &APlayerCharacter::Interact);
}

void APlayerCharacter::OnCapsuleBeginOverlap(UPrimitiveComponent *OverlappedComponent, AActor *OtherActor,
	UPrimitiveComponent *OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult &SweepResult)
{
	if (OtherComp->ComponentHasTag("Exit"))
		OnGamePlayStateChange.Broadcast(EGamePlayState::Won);
}

void APlayerCharacter::OnCharacterTakeDamage(AActor *DamagedActor,
	float Damage, const UDamageType *DamageType, AController *InstigatedBy, AActor *DamageCauser)
{
	float NewHealth = Health + Damage;
	if (NewHealth > 0.0f) {
		if (NewHealth > 1.0f)
			NewHealth = 1.0f;
		HUDWBP->HealthProgressBar->SetPercent(Health = NewHealth);
	} else if (NewHealth <= 0.0f) {
		OnGamePlayStateChange.Broadcast(EGamePlayState::Dead);
	}
}