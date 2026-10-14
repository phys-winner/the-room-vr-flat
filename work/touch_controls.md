# TOUCH_CONTROLS — The Room VR: A Dark Matter (PICO 1.02) → flat touch

Inputs: original/original.apk sha256 d6608b54…e65e (libil2cpp build-id
689e6add5baa2220aab9a52be8eaf486284038ff), port sources src/roomfix (libroomfix), src/java/port/roomfix (HUD,
routing). Device evidence: redmi-note8pro (Redmi Note 8 Pro, Android 11, Mali-G76), runs R003–R010.

## Baseline (original)
The PICO build has no touch or gamepad path at all: every action is a 6-DoF controller pose plus buttons read
through Unity XR InputDevices (PICO porting PVRInput → SantaCruzController → Rewired custom controllers:
axes 0/1 stick, 2 trigger, 3 grip; buttons 10 A/X, 11 B/Y, 12 menu). On a phone the original stops at the PICO
platform dialog (R001), so the whole action set needs a flat path. Rewired has no keyboard/gamepad maps that a phone
could use (no gamepad path to preserve; a gamepad scheme is an optional stage-8 candidate).

## Architecture of the port
- Synthetic XR devices (rf_xr.c): head pose from look input; two hand controllers whose poses/buttons are set by
  touch actions; all reads go through the game's own XR → PVRInput → Rewired → Player/Controller code.
- Java (RoomFix.onTouch): per-pointer ownership — a finger that lands on a HUD button belongs to it; every other
  finger goes to libroomfix with coordinates normalised to the Unity view (window letterbox/cutout aware).
- HUD (HudView): pause, teleport, inventory, eyepiece, snap-turn arrows; visibility from the game's input gates
  (Player.TeleportInput/InventoryInput/GogglesInput/SnapTurnInput + GogglesHudManager enabled); opted out of
  vendor force-dark. Back key = menu back / pause.
- Gestures (rf_touch.c / rf_interact.c): tap, drag, long press, two-finger pinch, two-finger hold+look.

## Action map (VR column and native point)

| Action | VR (original) | Flat touch | Native point used | Context | Evidence |
|---|---|---|---|---|---|
| Look around | head rotation | one-finger drag on empty space (100° per screen width) | head pose node (PoseDataSource / XR head device) | gameplay, menus | R003, R009 |
| Lean in to inspect | lean body | two-finger spread/pinch (0–0.35 m along the view) | head position | gameplay | R010 (lean 0.32 m) |
| Snap turn | stick left/right | HUD ↶ ↷ (shown while the tutorial asks) | stick X pulse → RwAction 6/7 | tutorial | R004, R009 |
| Teleport | stick forward, aim arc, release | HUD teleport → tap a glowing location marker | TeleportPointer aimed at PlayerLocation, stick release | gameplay | R004, R007, R009 |
| Arrival view | body faces location | camera turns to the location's forward (−20°) | head yaw from PlayerLocation.forward | after teleport | R009 |
| Superzoom (enter/exit miniature) | stick forward at crystals | teleport mode → tap the portal | TeleportPointer at Superzoom ZoomIn/Portal/ZoomOut trigger | chapters | implemented; not reached on device (debt: first crystals, chapel level) |
| Grab and move objects (levers, handles) | grip + arm motion | press on the object, drag | right Controller.Grabber pose; GrabbableCollider math volumes; constraint-aware drag (slider line, rotation plane, twist) | gameplay | R005, R009, R010 |
| Sliders (drawers, grilles, tabs) | grip + pull | drag along the slide | SliderGrabbable LocalSlideAxis | gameplay, inventory | R009, R010 |
| Knobs / keys (wrist twist) | twist wrist | finger on the knob, out to the side, circle around it (multi-turn) | hand rotation about RotateAxis (TwistControl) | gameplay | R006, R009, R010 |
| Position-driven knobs | turn hand around | same circle gesture | off-axis grab 2 cm from the axis | gameplay | R010 |
| Cranks | circle the arm | circle around the axis, any number of turns | grab point on the circle (K_ROTATION) | gameplay | R010 (2891°) |
| Pick up item (proxy) | grip, release | long press, lift | InventoryItemProxy grab/release → inventory | gameplay | R009 |
| Read notes | hold in front of eyes | long press | same | gameplay | R009 |
| Carry item to a place | carry in hand | drag; item follows the surface under the finger | depth from physics raycast (ignores held item, inventory grid) | gameplay | R010 |
| Inventory open/close | A / X button | HUD inventory | right primary pulse (RwAction 21) → HudManager.ToggleOnOff; grid follows camera pitch | gameplay | R007, R010 |
| Take item from inventory | point finger at slot + grip | press on the slot | PointingFingerTip ray (InventoryHud.RaycastInventory) | inventory | R006, R009 |
| Use item on receiver (keyhole, socket) | carry to receiver, release | drag onto it, lift | receiver magnet (Controller.InventoryPoint probe formula) | gameplay | R006, R009, R010 |
| Grab hand-over (key becomes lock) | regrip | automatic release + re-grab | G_REGRAB | gameplay | R006, R009 |
| Hints | pull ? tab, then hint tab | drag the tabs | SliderGrabbable "Hints Tab n Grabbable" | inventory | R010 |
| Eyepiece on/off | pull the inventory tab (or PS button) | HUD eyepiece, or drag the tab | GogglesHudManager.Activate/DeactivateWithFade (same as Player.UpdateGogglesInput) / tab RotationGrabbable | gameplay | R010 |
| Hold object + look around | hand holds, head turns | finger 1 holds, finger 2 swipes | grab + head yaw | gameplay | R010 |
| HUD while holding | — | other finger on a HUD button | per-pointer ownership | gameplay | R010 |
| Menu buttons | laser + trigger | tap | right ray on the camera ray through the finger + trigger; pointer hand-over press | main/pause menus | R003, R007 |
| Option selectors | laser + trigger on arrows | tap the arrows | UiOptionSelector buttons | options | R008 |
| Back | — | Android Back: previous menu screen / resume / pause | MenuController.TransitionToScreen, left menu (RwAction 22/27) | all | R007, R008 |
| Pause | left menu button | HUD pause, Back | GameStateManager.Pause | gameplay | R007 |
| Posture wizard (VR) | choose sitting/standing | answered automatically (Sitting) | PostureSelectScreen.SittingPressed | front end | R007 |
| Recenter / height calibration / turn fade / VR controls map | VR settings | hidden | OptionsScreen rows hidden | options | R008 |
| Poke buttons (piano keys, buttons) | push with fingertip | tap (to implement when first reached) | PokeProbe / CanBePoked volumes | later levels | debt: Egypt/organ levels |
| Ropes | pull rope hand over hand | drag (expected through RopeGrabbable as slider) | RopeGrabbable | later levels | debt: chapel level ropes |
| Text input | none in the game | — | — | — | no text entry exists |
| Save / load | autosave, Continue | Continue game / profiles by tap | SaveManager (external files dir) | front end | R009 continue runs |

## Multi-touch rules
- Stable pointer ids; HUD fingers never reach the game; the primary game finger owns grab/look; a second finger
  pinches (when looking) or turns the view (when holding). Lifting the primary ends the gesture; orphan moves are
  ignored until a new down. Window focus loss cancels all fingers (no stuck grip); CANCEL releases grabs.

## Remaining work / debts (TOUCH_GAMEPLAY_VERIFIED)
- Full cycle cold start → new game → tutorial → save → restart/resume: done up to the open safe + continue (R009,
  R010); the rest of the tutorial needs puzzle solving, not new controls.
- Superzoom, poke buttons, ropes: reach on device (chapters) — exact places: first crystals (chapel), organ keys.
- First-run hint for the gestures (in-game hint or PORT_FEATURES.md).
