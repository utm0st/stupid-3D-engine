#pragma once

#include "l_entity.hpp"
#include <unordered_set>

namespace lain {

class level_editor_camera3d;
class level_editor_renderer;
class input_manager;
class event_manager;
class entity_component_system;
class resource_manager;

enum class level_editor_mode { move, edit };

class level_editor final {
public:
  level_editor(level_editor_camera3d& camera, input_manager& inputManager,
               event_manager& eventManager, entity_component_system& ecs,
               resource_manager& resourceManager);

  // TODO: bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad
  // bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad bad
  void ProcessInput(float const deltaTime);

  void Update(float const deltaTime);

  void Render();

  int GetSquaresToDraw() const {
    return static_cast<int>((2 * GetHalfGridExtent()) / GetGridSquareSize());
  }

  int GetGridLinesToDraw() const { return GetSquaresToDraw() * 2 * 2 * 3; }

  float GetHalfGridExtent() const { return _halfGridExtent; }

  float GetGridSquareSize() const { return _gridSquareSize; }

  void SetRenderer(level_editor_renderer* renderer);

private:
  void ProcessKeyboardInMoveMode(float const deltaTime);

  void ProcessCursorInMoveMode();

  void ProcessKeyboardInEditMode();

  void SpawnMazeModel();

  void SpawnBallModel();

  void ClearModels();

  std::unordered_set<entity_id> _entities;
  level_editor_mode _mode;
  level_editor_renderer* _renderer;
  level_editor_camera3d& _camera;
  input_manager& _inputManager;
  event_manager& _eventManager;
  entity_component_system& _ecs;
  resource_manager& _resourceManager;
  float const _gridSquareSize;
  float const _halfGridExtent;
  entity_id _selectedEntity;
};

}; // namespace lain
