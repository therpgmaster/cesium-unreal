// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "CesiumFeatureId.h"
#include "CesiumFeatureTable.h"
#include "CesiumGltf/Accessor.h"
#include "CesiumGltf/ExtensionExtMeshFeaturesFeatureId.h"
#include "CesiumGltf/Model.h"

using namespace CesiumGltf;

static FCesiumFeatureIDAttribute EmptyFeatureIDAttribute;
static FCesiumFeatureIDTexture EmptyFeatureIDTexture;

FCesiumFeatureID::FCesiumFeatureID(
    const Model& InModel,
    const MeshPrimitive& Primitive,
    const ExtensionExtMeshFeaturesFeatureId& FeatureID)
    : _featureID(),
      _featureIDType(ECesiumFeatureIDType::None),
      _featureCount(FeatureID.featureCount),
      _propertyTableIndex() {
  if (FeatureID.propertyTable) {
    _propertyTableIndex = *FeatureID.propertyTable;
  }

  if (FeatureID.attribute) {
    _featureID =
        FCesiumFeatureIDAttribute(InModel, Primitive, *FeatureID.attribute);
    _featureIDType = ECesiumFeatureIDType::Attribute;

    return;
  }

  if (FeatureID.texture) {
    _featureID = FCesiumFeatureIDTexture(InModel, *FeatureID.texture);
    _featureIDType = ECesiumFeatureIDType::Texture;

    return;
  }

  if (_featureCount > 0) {
    _featureIDType = ECesiumFeatureIDType::Implicit;
  }
}

const ECesiumFeatureIDType UCesiumFeatureIDBlueprintLibrary::GetFeatureIDType(
    UPARAM(ref) const FCesiumFeatureID& FeatureID) {
  return FeatureID._featureIDType;
}

const FCesiumFeatureIDAttribute
UCesiumFeatureIDBlueprintLibrary::GetAsFeatureIDAttribute(
    UPARAM(ref) const FCesiumFeatureID& FeatureID) {
  if (FeatureID._featureIDType == ECesiumFeatureIDType::Attribute) {
    return std::get<FCesiumFeatureIDAttribute>(FeatureID._featureID);
  }

  return EmptyFeatureIDAttribute;
}

const FCesiumFeatureIDTexture
UCesiumFeatureIDBlueprintLibrary::GetAsFeatureIDTexture(
    UPARAM(ref) const FCesiumFeatureID& FeatureID) {
  if (FeatureID._featureIDType == ECesiumFeatureIDType::Texture) {
    return std::get<FCesiumFeatureIDTexture>(FeatureID._featureID);
  }

  return EmptyFeatureIDTexture;
}

const int64 UCesiumFeatureIDBlueprintLibrary::GetPropertyTableIndex(
    UPARAM(ref) const FCesiumFeatureID& FeatureID) {
  return FeatureID._propertyTableIndex.IsSet()
             ? FeatureID._propertyTableIndex.GetValue()
             : -1;
}

int64 UCesiumFeatureIDBlueprintLibrary::GetFeatureCount(
    UPARAM(ref) const FCesiumFeatureID& FeatureID) {
  return FeatureID._featureCount;
}

int64 UCesiumFeatureIDBlueprintLibrary::GetFeatureIDForVertex(
    UPARAM(ref) const FCesiumFeatureID& FeatureID,
    int64 VertexIndex) {
  if (FeatureID._featureIDType == ECesiumFeatureIDType::Attribute) {
    FCesiumFeatureIDAttribute attribute =
        std::get<FCesiumFeatureIDAttribute>(FeatureID._featureID);
    return UCesiumFeatureIDAttributeBlueprintLibrary::GetFeatureIDForVertex(
        attribute,
        VertexIndex);
  }

  if (FeatureID._featureIDType == ECesiumFeatureIDType::Texture) {
    // TODO:
  }

  if (FeatureID._featureIDType == ECesiumFeatureIDType::Implicit) {
    return VertexIndex;
  }

  return -1;
}
