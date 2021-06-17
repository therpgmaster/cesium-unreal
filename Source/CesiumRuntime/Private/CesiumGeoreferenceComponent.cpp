// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "CesiumGeoreferenceComponent.h"
#include "CesiumGeospatial/Cartographic.h"
#include "CesiumGeospatial/Ellipsoid.h"
#include "CesiumGeospatial/Transforms.h"
#include "CesiumRuntime.h"
#include "CesiumTransforms.h"
#include "CesiumUtility/Math.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "UObject/NameTypes.h"
#include "VecMath.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>
#include <optional>

UCesiumGeoreferenceComponent::UCesiumGeoreferenceComponent()
    : _worldOriginLocation(0.0),
      _absoluteLocation(0.0),
      _relativeLocation(0.0),
      //_actorToECEF(),
      _actorToUnrealRelativeWorld(),
      _ownerRoot(nullptr),
      _ignoreOnUpdateTransform(false),
      //_autoSnapToEastSouthUp(false),
      _dirty(false) {
  this->bAutoActivate = true;
  this->bWantsOnUpdateTransform = true;
  this->bWantsInitializeComponent = true;

  PrimaryComponentTick.bCanEverTick = false;

  // TODO: check when exactly constructor is called. Is it possible that it is
  // only called for CDO and then all other load/save/replications happen from
  // serialize/deserialize?

  // set a delegate callback when the root component for the actor is set
  this->IsRootComponentChanged.AddDynamic(
      this,
      &UCesiumGeoreferenceComponent::OnRootComponentChanged);
}

void UCesiumGeoreferenceComponent::SnapLocalUpToEllipsoidNormal() {
  // local up in ECEF (the +Z axis)
  glm::dvec3 actorUpECEF = glm::normalize(this->_actorToECEF[2]);

  // the surface normal of the ellipsoid model of the globe at the ECEF location
  // of the actor
  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }
  glm::dvec3 ellipsoidNormal =
      this->Georeference->ComputeGeodeticSurfaceNormal(this->_actorToECEF[3]);

  // the shortest rotation to align local up with the ellipsoid normal
  glm::dquat R = glm::rotation(actorUpECEF, ellipsoidNormal);

  // We only want to apply the rotation to the actor's orientation, not
  // translation.
  this->_actorToECEF[0] = R * this->_actorToECEF[0];
  this->_actorToECEF[1] = R * this->_actorToECEF[1];
  this->_actorToECEF[2] = R * this->_actorToECEF[2];

  this->_updateActorToUnrealRelativeWorldTransform();
  this->_setTransform(this->_actorToUnrealRelativeWorld);
}

void UCesiumGeoreferenceComponent::SnapToEastSouthUp() {

  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }

  // TODO GEOREF_REFACTORING
  // The following line should be replaced by the commented-out one,
  // but the corresponding function from GeoRef returns a mat3,
  // omitting the translation component
  glm::dmat4 ENUtoECEF = CesiumGeospatial::Transforms::eastNorthUpToFixedFrame(
      this->_actorToECEF[3],
      CesiumGeospatial::Ellipsoid::WGS84);
  // glm::dmat4 ENUtoECEF =
  //    this->Georeference->ComputeEastNorthUpToEcef(this->_actorToECEF[3]);

  this->_actorToECEF = ENUtoECEF * CesiumTransforms::scaleToCesium *
                       CesiumTransforms::unrealToOrFromCesium;

  this->_updateActorToUnrealRelativeWorldTransform();
  this->_setTransform(this->_actorToUnrealRelativeWorld);
}

void UCesiumGeoreferenceComponent::MoveToLongitudeLatitudeHeight(
    const glm::dvec3& targetLongitudeLatitudeHeight,
    bool maintainRelativeOrientation) {

  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }
  glm::dvec3 ecef = this->Georeference->TransformLongitudeLatitudeHeightToEcef(
      targetLongitudeLatitudeHeight);

  this->_setECEF(ecef, maintainRelativeOrientation);
}

void UCesiumGeoreferenceComponent::InaccurateMoveToLongitudeLatitudeHeight(
    const FVector& targetLongitudeLatitudeHeight,
    bool maintainRelativeOrientation) {
  this->MoveToLongitudeLatitudeHeight(
      VecMath::createVector3D(targetLongitudeLatitudeHeight),
      maintainRelativeOrientation);
}

void UCesiumGeoreferenceComponent::MoveToECEF(
    const glm::dvec3& targetEcef,
    bool maintainRelativeOrientation) {
  this->_setECEF(targetEcef, maintainRelativeOrientation);
}

void UCesiumGeoreferenceComponent::InaccurateMoveToECEF(
    const FVector& targetEcef,
    bool maintainRelativeOrientation) {
  this->MoveToECEF(
      VecMath::createVector3D(targetEcef),
      maintainRelativeOrientation);
}

// TODO: is this the best place to attach to the root component of the owner
// actor?
void UCesiumGeoreferenceComponent::OnRegister() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called OnRegister on component %s"),
      *this->GetName());
  Super::OnRegister();
  this->_initRootComponent();

  if (!this->Georeference) {
    this->Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);
  }
  if (this->Georeference) {
    this->_updateActorToUnrealRelativeWorldTransform();
    this->_updateActorToECEF();

    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Attaching CesiumGeoreferenceComponent callback to Georeference %s"),
        *this->GetFullName());

    this->Georeference->OnGeoreferenceUpdated.AddUniqueDynamic(
        this,
        &UCesiumGeoreferenceComponent::HandleGeoreferenceUpdated);
    HandleGeoreferenceUpdated();
  }
}

// TODO: figure out what these delegate parameters actually represent, currently
// I'm only guessing based on the types
void UCesiumGeoreferenceComponent::OnRootComponentChanged(
    USceneComponent* UpdatedComponent,
    bool bIsRootComponent) {
  this->_initRootComponent();
}

void UCesiumGeoreferenceComponent::ApplyWorldOffset(
    const FVector& InOffset,
    bool bWorldShift) {
  // USceneComponent::ApplyWorldOffset will call OnUpdateTransform, we want to
  // ignore it since we don't have to recompute everything on origin rebase.
  this->_ignoreOnUpdateTransform = true;
  USceneComponent::ApplyWorldOffset(InOffset, bWorldShift);

  const FIntVector& oldOrigin = this->GetWorld()->OriginLocation;
  this->_worldOriginLocation = VecMath::subtract3D(oldOrigin, InOffset);

  // Do _not_ call _updateAbsoluteLocation. The absolute position doesn't change
  // with an origin rebase, and we'll lose precision if we update the absolute
  // location here.

  this->_updateRelativeLocation();
  this->_updateActorToUnrealRelativeWorldTransform();
  if (this->FixTransformOnOriginRebase) {
    this->_setTransform(this->_actorToUnrealRelativeWorld);
  }
}

void UCesiumGeoreferenceComponent::OnUpdateTransform(
    EUpdateTransformFlags UpdateTransformFlags,
    ETeleportType Teleport) {
  USceneComponent::OnUpdateTransform(UpdateTransformFlags, Teleport);

  // if we generated this transform call internally, we should ignore it
  if (this->_ignoreOnUpdateTransform) {
    this->_ignoreOnUpdateTransform = false;
    return;
  }

  this->_updateAbsoluteLocation();
  this->_updateRelativeLocation();
  this->_updateActorToECEF();
  this->_updateActorToUnrealRelativeWorldTransform();

  // TODO: add warning or fix unstable behavior when autosnapping a translation
  // in terms of the local axes
  if (this->_autoSnapToEastSouthUp) {
    this->SnapToEastSouthUp();
  }
}

bool UCesiumGeoreferenceComponent::MoveComponentImpl(
    const FVector& Delta,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutHit,
    EMoveComponentFlags MoveFlags,
    ETeleportType Teleport) {
  if (this->_ownerRoot != this) {
    return false;
  }
  return USceneComponent::MoveComponentImpl(
      Delta,
      NewRotation,
      bSweep,
      OutHit,
      MoveFlags,
      Teleport);
}

#if WITH_EDITOR
void UCesiumGeoreferenceComponent::PostEditChangeProperty(
    FPropertyChangedEvent& event) {
  Super::PostEditChangeProperty(event);

  if (!event.Property) {
    return;
  }

  FName propertyName = event.Property->GetFName();

  if (propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, Longitude) ||
      propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, Latitude) ||
      propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, Height)) {
    this->MoveToLongitudeLatitudeHeight(
        glm::dvec3(this->Longitude, this->Latitude, this->Height));
    return;
  } else if (
      propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, ECEF_X) ||
      propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, ECEF_Y) ||
      propertyName ==
          GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, ECEF_Z)) {
    this->MoveToECEF(glm::dvec3(this->ECEF_X, this->ECEF_Y, this->ECEF_Z));
    return;
  } else if (propertyName == GET_MEMBER_NAME_CHECKED(UCesiumGeoreferenceComponent, Georeference)) {
    if (IsValid(this->Georeference)) {
      this->Georeference->OnGeoreferenceUpdated.AddUniqueDynamic(
          this,
          &UCesiumGeoreferenceComponent::HandleGeoreferenceUpdated);
    }
    return;
  }
}
#endif

void UCesiumGeoreferenceComponent::HandleGeoreferenceUpdated() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called HandleGeoreferenceUpdated for %s"),
      *this->GetName());
  this->_updateActorToUnrealRelativeWorldTransform();
  
  // TODO GEOREF_REFACTORING: Figure what this function is 
  // doing, and whether it has to be called here...
  //this->_updateActorToECEF();

  this->_setTransform(this->_actorToUnrealRelativeWorld);
}

void UCesiumGeoreferenceComponent::SetAutoSnapToEastSouthUp(bool value) {
  this->_autoSnapToEastSouthUp = value;
  if (value) {
    this->SnapToEastSouthUp();
  }
}



// TODO GEOREF_REFACTORING Remove these overrides,
// and the "bWantsInitializeComponent=true" that
// is set in the constructor!
void UCesiumGeoreferenceComponent::InitializeComponent() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called InitializeComponent on actor %s"),
      *this->GetName());
  Super::InitializeComponent();
}
void UCesiumGeoreferenceComponent::PostInitProperties() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called PostInitProperties on component %s"),
      *this->GetName());
  Super::PostInitProperties();
}

/**
 *  PRIVATE HELPER FUNCTIONS
 */

void UCesiumGeoreferenceComponent::_initRootComponent() {
  AActor* owner = this->GetOwner();
  this->_ownerRoot = owner->GetRootComponent();

  if (!this->_ownerRoot || !this->GetWorld()) {
    return;
  }

  // if this is not the root component, we need to attach to the root component
  // and control it
  if (this->_ownerRoot != this) {
    this->AttachToComponent(
        this->_ownerRoot,
        FAttachmentTransformRules::SnapToTargetIncludingScale);
  }

  this->_initWorldOriginLocation();
  this->_updateAbsoluteLocation();
  this->_updateRelativeLocation();
}

void UCesiumGeoreferenceComponent::_initWorldOriginLocation() {
  const FIntVector& origin = this->GetWorld()->OriginLocation;
  this->_worldOriginLocation = VecMath::createVector3D(origin);
}

void UCesiumGeoreferenceComponent::_updateAbsoluteLocation() {
  const FVector& relativeLocation = this->_ownerRoot->GetComponentLocation();
  const FIntVector& originLocation = this->GetWorld()->OriginLocation;
  this->_absoluteLocation = VecMath::add3D(originLocation, relativeLocation);
}

void UCesiumGeoreferenceComponent::_updateRelativeLocation() {
  // Note: Since we have a presumably accurate _absoluteLocation, this will be
  // more accurate than querying the floating-point UE relative world location.
  // This means that although the rendering, physics, and anything else on the
  // UE side might be jittery, our internal representation of the location will
  // remain accurate.
  this->_relativeLocation =
      this->_absoluteLocation - this->_worldOriginLocation;
}

// this is what georeferences the actor
void UCesiumGeoreferenceComponent::_updateActorToECEF() {
  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }
  if (!IsValid(this->_ownerRoot)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid ownerRoot"));
    return;
  }

  const glm::dmat4& unrealWorldToEcef =
      this->Georeference->GetUnrealWorldToEllipsoidCenteredTransform();

  FMatrix actorToRelativeWorld =
      this->_ownerRoot->GetComponentToWorld().ToMatrixWithScale();
  glm::dmat4 actorToAbsoluteWorld =
      VecMath::createMatrix4D(actorToRelativeWorld, this->_absoluteLocation);

  this->_actorToECEF = unrealWorldToEcef * actorToAbsoluteWorld;
  this->_updateDisplayECEF();
  this->_updateDisplayLongitudeLatitudeHeight();
}

void UCesiumGeoreferenceComponent::
    _updateActorToUnrealRelativeWorldTransform() {
  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }
  const glm::dmat4& ecefToUnrealWorld =
      this->Georeference->GetEllipsoidCenteredToUnrealWorldTransform();
  glm::dmat4 absoluteToRelativeWorld = VecMath::createTranslationMatrix4D(
      -this->_worldOriginLocation.x,
      -this->_worldOriginLocation.y,
      -this->_worldOriginLocation.z,
      1.0);

  this->_actorToUnrealRelativeWorld =
      absoluteToRelativeWorld * ecefToUnrealWorld * this->_actorToECEF;
}

void UCesiumGeoreferenceComponent::_setTransform(const glm::dmat4& transform) {
  if (!this->GetWorld()) {
    return;
  }

  // We are about to get an OnUpdateTransform callback for this, so we
  // preemptively mark down to ignore it.
  _ignoreOnUpdateTransform = true;

  this->_ownerRoot->SetWorldTransform(
      FTransform(VecMath::createMatrix(transform)),
      false,
      nullptr,
      TeleportWhenUpdatingTransform ? ETeleportType::TeleportPhysics
                                    : ETeleportType::None);
  // TODO: try direct setting of transformation, may work for static objects on
  // origin rebase
  /*
  this->_ownerRoot->SetRelativeLocation_Direct(
      FVector(transform[3].x, transform[3].y, transform[3].z));

  this->_ownerRoot->SetRelativeRotation_Direct(FMatrix(
    FVector(transform[0].x, transform[0].y, transform[0].z),
    FVector(transform[1].x, transform[1].y, transform[1].z),
    FVector(transform[2].x, transform[2].y, transform[2].z),
    FVector(0.0, 0.0, 0.0)
  ).Rotator());

  this->_ownerRoot->SetComponentToWorld(this->_ownerRoot->GetRelativeTransform());
  */
}

void UCesiumGeoreferenceComponent::_setECEF(
    const glm::dvec3& targetEcef,
    bool maintainRelativeOrientation) {
  if (!maintainRelativeOrientation) {
    this->_actorToECEF[3] = glm::dvec4(targetEcef, 1.0);
  } else {
    // Note: this probably degenerates when starting at or moving to either of
    // the poles

    if (!IsValid(this->Georeference)) {
      UE_LOG(
          LogCesium,
          Warning,
          TEXT(
              "CesiumGeoreferenceComponent does not have a valid Georeference"));
      return;
    }
    // TODO GEOREF_REFACTORING
    // The following lines should be replaced by the commented-out ones,
    // but the corresponding functions from GeoRef returns a mat3,
    // omitting the translation component
    glm::dmat4 startEcefToEnu = glm::affineInverse(
        CesiumGeospatial::Transforms::eastNorthUpToFixedFrame(
            this->_actorToECEF[3],
            CesiumGeospatial::Ellipsoid::WGS84));
    glm::dmat4 endEnuToEcef =
        CesiumGeospatial::Transforms::eastNorthUpToFixedFrame(
            targetEcef,
            CesiumGeospatial::Ellipsoid::WGS84);
    // glm::dmat4 startEcefToEnu = glm::affineInverse(
    //    this->Georeference->ComputeEastNorthUpToEcef(this->_actorToECEF[3]));
    // glm::dmat4 endEnuToEcef =
    //    this->Georeference->ComputeEastNorthUpToEcef(targetEcef);

    this->_actorToECEF = endEnuToEcef * startEcefToEnu * this->_actorToECEF;
  }

  this->_updateActorToUnrealRelativeWorldTransform();
  this->_setTransform(this->_actorToUnrealRelativeWorld);

  // In this case the ground truth is the newly updated _actorToECEF
  // transformation, so it will be more accurate to compute the new Unreal
  // locations this way (as opposed to _updateAbsoluteLocation /
  // _updateRelativeLocation).
  this->_relativeLocation = this->_actorToUnrealRelativeWorld[3];
  this->_absoluteLocation =
      this->_relativeLocation + this->_worldOriginLocation;

  // If the transform needs to be snapped to the tangent plane, do it here.
  if (this->_autoSnapToEastSouthUp) {
    this->SnapToEastSouthUp();
  }

  // Update component properties
  this->_updateDisplayECEF();
  this->_updateDisplayLongitudeLatitudeHeight();
}

void UCesiumGeoreferenceComponent::_updateDisplayLongitudeLatitudeHeight() {

  if (!IsValid(this->Georeference)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT("CesiumGeoreferenceComponent does not have a valid Georeference"));
    return;
  }
  glm::dvec3 cartographic =
      this->Georeference->TransformEcefToLongitudeLatitudeHeight(
          this->_actorToECEF[3]);
  this->_dirty = true;
  this->Longitude = cartographic.x;
  this->Latitude = cartographic.y;
  this->Height = cartographic.z;
}

void UCesiumGeoreferenceComponent::_updateDisplayECEF() {
  this->_dirty = true;

  this->ECEF_X = this->_actorToECEF[3].x;
  this->ECEF_Y = this->_actorToECEF[3].y;
  this->ECEF_Z = this->_actorToECEF[3].z;
}
