// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#if WITH_EDITOR

#include <stack>
#include <algorithm>
#include <vector>
#include <random>

#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Cesium3DTileset.h"
#include "CesiumFlyToComponent.h"
#include "CesiumGeoreference.h"
#include "GlobeAwareDefaultPawn.h"
#include "GoogleTilesTestSetup.h"

#include "CesiumLoadTestCore.h"
#include "CesiumSceneGeneration.h"
#include "CesiumTestHelpers.h"

#include "TestRegionPolygons.h"

#define VIEWPORT_WIDTH 1280;
#define VIEWPORT_HEIGHT 720;
// Twelve hour soak test
constexpr static double SOAK_TEST_DURATION = 60 * 60 * 12;
// The duration in seconds between each stress test iteration
constexpr static double TEST_ITERATION_DELAY = 10.0;
constexpr static float FLIGHT_TIME = 5.0f;

// Stack of indices into TestRegionPolygons::polygons to use next
static std::stack<int> nextPolygonIndex;

namespace Cesium {

void fillWithRandomIndices() {
  // Create a vector with every index
  std::vector<int> indices;
  for (int i = 0; i < TEST_REGION_POLYGONS_COUNT; i++) {
    indices.push_back(i);
  }

  // Shuffle indices
  std::default_random_engine rng{};
  std::shuffle(indices.begin(), indices.end(), rng);

  // Push shuffled indices onto stack
  for (int idx : indices) {
    nextPolygonIndex.push(idx);
  }
}

// Since this shares a name with the global defined in Google3dTilesLoadTest, we
// tell the compiler to look for the variable there instead.
extern LoadTestContext gLoadTestContext;

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FFlyToRandomLocationCommand,
    LoadTestContext&,
    context);

bool FFlyToRandomLocationCommand::Update() {

  if (!GEditor->IsPlaySessionInProgress()) {
    return true;
  }

  UCesiumFlyToComponent* flyTo =
      context.playContext.pawn->FindComponentByClass<UCesiumFlyToComponent>();

  flyTo->Duration = FLIGHT_TIME;

  FVector pawnPosition = context.playContext.pawn->GetActorLocation();
  FVector llhPosition =
      context.playContext.georeference
          ->TransformUnrealPositionToLongitudeLatitudeHeight(pawnPosition);

  if (nextPolygonIndex.empty()) {
    fillWithRandomIndices();
  }

  const int nextIndex = nextPolygonIndex.top();
  nextPolygonIndex.pop();

  FVector targetLlh = TestRegionPolygons::polygons[nextIndex].GetRandomPoint();
  targetLlh.Z = 1000.0f;

  // Start the flight
  context.playContext.pawn
      ->FlyToLocationLongitudeLatitudeHeight(targetLlh, 0, 0, false);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGoogleTilesStressTest,
    "Cesium.Performance.StressTest.GoogleTiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FGoogleTilesStressTest::RunTest(const FString& Parameters) {

  LoadTestContext& context = gLoadTestContext;

  context.reset();

  UE_LOG(LogCesium, Display, TEXT("Creating common world objects..."));
  createCommonWorldObjects(context.creationContext);

  UE_LOG(LogCesium, Display, TEXT("Setting up location..."));
  GoogleTilesTestSetup::setupForGoogleplex(context.creationContext);
  ACesium3DTileset* tileset = context.creationContext.tilesets.at(0);
  tileset->MaximumCachedBytes = 0;
  context.creationContext.trackForPlay();

  // Let the editor viewports see the same thing the test will
  context.creationContext.syncWorldCamera();

  context.creationContext.refreshTilesets();

  ADD_LATENT_AUTOMATION_COMMAND(FWaitForShadersToFinishCompiling);

  // Queue play in editor and set desired viewport size
  FRequestPlaySessionParams Params;
  Params.WorldType = EPlaySessionWorldType::PlayInEditor;
  Params.EditorPlaySettings = NewObject<ULevelEditorPlaySettings>();
  Params.EditorPlaySettings->NewWindowWidth = VIEWPORT_WIDTH;
  Params.EditorPlaySettings->NewWindowHeight = VIEWPORT_HEIGHT;
  Params.EditorPlaySettings->EnableGameSound = false;
  GEditor->RequestPlaySession(Params);

  ADD_LATENT_AUTOMATION_COMMAND(
      InitForPlayWhenReady(context.creationContext, context.playContext));

  // Wait to show distinct gap in profiler
  ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

  int numFlights = static_cast<int>(
      SOAK_TEST_DURATION / (FLIGHT_TIME + TEST_ITERATION_DELAY));

  for (int i = 0; i < numFlights; i++) {
    // Give it some time for the tiles to load where we are
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(TEST_ITERATION_DELAY));
    ADD_LATENT_AUTOMATION_COMMAND(FFlyToRandomLocationCommand(context));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(FLIGHT_TIME));
  }

  // End play in editor
  ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());

  ADD_LATENT_AUTOMATION_COMMAND(TestCleanupCommand(context));

  return true;
}

} // namespace Cesium

#endif
