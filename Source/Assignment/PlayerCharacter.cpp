#include "PlayerCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

#include "Interactable.h"
#include "Kismet/KismetMaterialLibrary.h"

static const FVector AimOffset(0.0f, 64.0f, 90.0f);

APlayerCharacter::APlayerCharacter()
	: InteractableItem(nullptr), Health(1.0f), FirstAidKitNumber(0), KeyNumber(0), FuseNumber(0), bArmed(false)
{
	PrimaryActorTick.bCanEverTick = true;

	/* Capsule Component */
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);
	GetCapsuleComponent()->SetNotifyRigidBodyCollision(false);
	GetCapsuleComponent()->OnComponentBeginOverlap.AddDynamic(this, &APlayerCharacter::OnCapsuleBeginOverlap);

	/* Set Skeletal Mesh */
	static ConstructorHelpers::FObjectFinder<USkeletalMesh>
		CharacterSKMeshAsset(TEXT("SkeletalMesh'/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin'"));
	GetMesh()->SetSkeletalMesh(CharacterSKMeshAsset.Object);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -94.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	/* Set Animation */
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	static ConstructorHelpers::FClassFinder<UAnimInstance>
		AnimBPClass(TEXT("AnimBlueprint'/Game/Mannequin/Animations/ABP_Player.ABP_Player_C'"));
	GetMesh()->SetAnimInstanceClass(AnimBPClass.Class);

	/* Movement */
	GetCharacterMovement()->MaxWalkSpeed = 500.0f;

	/* Spring Arm Component */
	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm Component"));
	SpringArmComp->SetupAttachment(RootComponent);

	SpringArmComp->SocketOffset = AimOffset;
	SpringArmComp->TargetArmLength = 256.0f;
	SpringArmComp->bUsePawnControlRotation = true;

	/* Third Person Camera Component */
	TPCameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("Third Person Camera Component"));
	TPCameraComp->SetupAttachment(SpringArmComp);
	
	/* Flash Light Component */
	FlashLightComp = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flash Light Component"));
	FlashLightComp->SetupAttachment(SpringArmComp);

	FlashLightComp->SetRelativeLocation(FVector(16.0f, 0.0f, 32.0f));
	FlashLightComp->Intensity = 16384.0f;
	FlashLightComp->AttenuationRadius = 8192.0f;
	FlashLightComp->InnerConeAngle = 16.0f;
	FlashLightComp->OuterConeAngle = 32.0f;

	/* Grenade Launcher */
	GrenadeLauncher = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Grenade Launcher"));
	GrenadeLauncher->AttachToComponent(GetMesh(),
		FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), "GripPoint");

	static ConstructorHelpers::FObjectFinder<UStaticMesh>
		GrenadeLauncherAsset(TEXT("StaticMesh'/Game/Mannequin/Weapon/Mesh/SM_GrenadeLauncher.SM_GrenadeLauncher'"));
	GrenadeLauncher->SetStaticMesh(GrenadeLauncherAsset.Object);
	GrenadeLauncher->SetVisibility(false);

	/* Pant Sound Component */
	PantAudioComp = CreateDefaultSubobject<UAudioComponent>(TEXT("Pant Audio Component"));
	PantAudioComp->SetupAttachment(RootComponent);

	static ConstructorHelpers::FObjectFinder<USoundWave> PantSoundAsset(TEXT("SoundWave'/Game/Audios/SW_Pant.SW_Pant'"));
	PantAudioComp->SetSound(PantSoundAsset.Object);

	PantAudioComp->bAutoActivate = false;

	/* Animation Assets Loading */
	static ConstructorHelpers::FObjectFinder<UAnimationAsset>
		FireAnimAsset(TEXT("AnimSequence'/Game/Mannequin/Animations/AS_Fire.AS_Fire'"));
	FireAnim = FireAnimAsset.Object;

	static ConstructorHelpers::FObjectFinder<UAnimationAsset>
        ReloadAnimAsset(TEXT("AnimSequence'/Game/Mannequin/Animations/AS_Reload.AS_Reload'"));
	ReloadAnim = ReloadAnimAsset.Object;

	static ConstructorHelpers::FObjectFinder<UAnimMontage>
        DeathAnimAsset(TEXT("AnimMontage'/Game/Mannequin/Animations/AM_Death.AM_Death'"));
	DeathAnim = DeathAnimAsset.Object;
	
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
	UClass *HUDWBPClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("WidgetBlueprint'/Game/UIs/WBP_HUD.WBP_HUD_C'"));
	HUDWBP = CreateWidget<UHUDUserWidget>(GetWorld(), HUDWBPClass);
	HUDWBP->AddToViewport(0);

	/* Create & Set Camera Blood Dynamic Post Process Material */
	UMaterial *CameraBloodPPMAsset = LoadObject<UMaterial>(nullptr, TEXT("Material'/Game/Materials/PPM_Blood.PPM_Blood'"));
	CameraBloodPPM = UKismetMaterialLibrary::CreateDynamicMaterialInstance(GetWorld(), CameraBloodPPMAsset);
	TPCameraComp->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, CameraBloodPPM));

	/* Bind Montage End Event */
	GetMesh()->GetAnimInstance()->OnMontageEnded.AddDynamic(this, &APlayerCharacter::OnDeathAnimEnded);
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TPCamTransform = TPCameraComp->GetComponentToWorld();
	TraceStart = TPCamTransform.GetLocation();
	TraceEnd = TraceStart + TPCamTransform.GetRotation().GetForwardVector() * 512.0f;

	if (GetWorld()->LineTraceSingleByChannel(LineTraceHitRes, TraceStart, TraceEnd, ECollisionChannel::ECC_Visibility)) {
		if (LineTraceHitRes.Actor.IsValid() && LineTraceHitRes.Actor->GetClass()->ImplementsInterface(UInteractable::StaticClass())) {
			InteractableItem = Cast<IInteractable>(LineTraceHitRes.Actor);
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

void APlayerCharacter::OnDeathAnimEnded(UAnimMontage *Montage, bool bInterrupted)
{
	if (Montage == DeathAnim)
		OnGamePlayStateChange.Broadcast(EGamePlayState::Dead);
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

		/* If Player Health is Below 25, Play the Sound */
		if (NewHealth < 0.25f)
			PantAudioComp->Activate();
		/* If Player Health is Above 25, Stop Playing the Sound */
		else if (NewHealth > Health && NewHealth >= 0.25f)
			PantAudioComp->Deactivate();

		/* Update Camera Blood Post Process Material */
		CameraBloodPPM->SetScalarParameterValue("Blood Inner Radius",FMath::Lerp(0.5f, 1.0f, NewHealth));

		/* Update HUD Health Progress Bar */
		HUDWBP->HealthProgressBar->SetPercent(Health = NewHealth);
	} else if (NewHealth <= 0.0f) {
		GetWorld()->GetFirstPlayerController()->SetViewTargetWithBlend(DeathCameraActor, 1.5f);
		GetMesh()->GetAnimInstance()->Montage_Play(DeathAnim);
	}
}
