# Native Script API

Catalyst exposes gameplay systems to `GameLogic.dll` through [`CatalystAPI.h`](../Scripting/CatalystAPI.h).

## Quick Start

1. Open the generated `Code/GameLogic.sln`.
2. Include `CatalystAPI.h`.
3. Derive from `Catalyst::NativeScript`.
4. Register each class with `REGISTER_SCRIPT(MyScript)`.
5. Keep exactly one `CATALYST_EXPORT_SCRIPT_MODULE();` in the project.
6. Build `GameLogic.dll`.
7. Assign the script from the editor Details panel and press Play.

## Lifecycle

- `OnStart()`: called once when play starts.
- `OnUpdate(float deltaTime)`: called every frame while the owning object is active.
- `OnStop()`: called when play ends or the owning object is destroyed.
- `OnHotReloadSave(...)`: save transient state before a DLL reload.
- `OnHotReloadLoad(...)`: restore transient state after a DLL reload.
- `OnCollisionEnter/Stay/Exit(...)`: solid collider callbacks.
- `OnTriggerEnter/Stay/Exit(...)`: trigger overlap callbacks.
- `OnTimer(uint64_t timerId)`: timer callback created with `SetTimer(...)`.

## Core Types

- `Catalyst::ObjectId`: stable runtime object handle.
- `Catalyst::ComponentId`: stable native-script component handle.
- `Catalyst::TransformData`: position, rotation, scale.
- `Catalyst::ColliderData`: trigger/box/sphere collider settings.
- `Catalyst::RigidbodyData`: rigidbody enable/body type/gravity/mass/damping/restitution/velocity.
- `Catalyst::RaycastHit`: hit object, point, normal, distance, trigger flag.
- `Catalyst::Vec3` / `Catalyst::Vec4`: math payloads used across the DLL boundary.

## Input

- `GetKeyDown(Key::W)`
- `GetKeyPressed(Key::Space)`
- `IsActionPressed("Jump")`
- `WasActionJustPressed("Fire")`
- `GetAxis("Horizontal")`
- `GetAxis("Vertical")`
- `GetAxis("MoveUp")`

Current default action names:

- `Jump`
- `Interact`
- `Use`
- `Sprint`
- `Fire`
- `PrimaryFire`
- `SecondaryFire`
- `Aim`
- `Crouch`
- `Reload`
- `Pause`

Current default axis names:

- `Horizontal`
- `MoveRight`
- `Vertical`
- `MoveForward`
- `Up`
- `MoveUp`

## Actor And World

- `GetObjectId()`
- `GetObjectName()`
- `GetObjectType()`
- `IsObjectEnabled()`
- `SetObjectEnabled(...)`
- `FindObjectByName(...)`
- `FindFirstObjectWithTag(...)`
- `GetObjectsWithTag(...)`
- `GetTag(...)`
- `SetTag(...)`
- `GetLayer(...)`
- `SetLayer(...)`
- `AttachToParent(...)`
- `DetachFromParent(...)`
- `DuplicateObject(...)`
- `DestroyObject(...)`
- `InstantiateFromAssetPath(...)`

Notes:

- Parenting uses a lightweight local-transform hierarchy.
- `InstantiateFromAssetPath(...)` currently instantiates from assets already known to the project/runtime.

## Transform, Physics, And Collision

- `GetTransform()`
- `SetTransform(...)`
- `Translate(...)`
- `GetCollider()`
- `SetCollider(...)`
- `GetRigidBody()`
- `SetRigidBody(...)`
- `AddForce(...)`
- `AddImpulse(...)`
- `Raycast(...)`

## Rendering And Camera

- `GetMaterialPath(...)`
- `SetMaterialPath(...)`
- `GetColor(...)`
- `SetColor(...)`
- `GetLightIntensity(...)`
- `SetLightIntensity(...)`
- `GetMainCameraFov()`
- `SetMainCameraFov(...)`

## UI

- `FindWidgetByText(...)`
- `SetWidgetText(...)`
- `SetWidgetVisible(...)`
- `SetWidgetTint(...)`

These APIs target runtime UI widgets spawned from Catalyst UI Blueprints.

## Time, Timers, And Save Data

- `GetScaledDeltaTime()`
- `GetUnscaledDeltaTime()`
- `GetTimeScale()`
- `SetTimeScale(...)`
- `SetTimer(timerId, delaySeconds, looping)`
- `CancelTimer(timerId)`
- `SaveString(...)`
- `SaveFloat(...)`
- `SaveBool(...)`
- `LoadString(...)`
- `LoadFloat(...)`
- `LoadBool(...)`

Save data is written per project under `SaveData/<slot>.catalystsave`.

## Animation

- `GetAnimationState(...)`
- `SetAnimationState(...)`
- `TriggerAnimation(...)`

Current support is a gameplay-facing animation state channel. It is ready for future skeletal animation systems to consume.

## Audio

- `PlayOneShot2D(...)`
- `PlayOneShot3D(...)`
- `StopAudio(...)`
- `SetAudioVolume(...)`
- `SetAudioPitch(...)`

The API surface is present now, but Catalyst does not yet ship with a full runtime mixer/spatial audio backend. Current calls are safe but act as stubs.

## Example

```cpp
#include "CatalystAPI.h"

class PlayerController final : public Catalyst::NativeScript {
public:
    const char* GetClassName() const override { return "PlayerController"; }

    void OnStart() override {
        SetTimer(1, 0.25f, true);
    }

    void OnUpdate(float deltaTime) override {
        Translate(GetAxis("Horizontal") * 250.0f * deltaTime, 0.0f, GetAxis("Vertical") * 250.0f * deltaTime);

        if (WasActionJustPressed("Jump")) {
            AddImpulse({0.0f, 6.0f, 0.0f});
        }
    }

    void OnTimer(uint64_t timerId) override {
        if (timerId == 1) {
            PrintToConsole("Tick");
        }
    }
};

REGISTER_SCRIPT(PlayerController)
```

## Hot Reload Rules

- Keep gameplay state you care about inside `OnHotReloadSave(...)` / `OnHotReloadLoad(...)`.
- Keep exactly one `CATALYST_EXPORT_SCRIPT_MODULE();` in the whole game-code project.
- Do not remove the generated export macro from the sample file unless you move it to another `.cpp`.
