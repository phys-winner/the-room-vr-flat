# TOUCH_CONTROLS — The Room VR: A Dark Matter (PICO 1.02) → flat touch

Inputs: original/original.apk sha256 d6608b54…e65e (libil2cpp build-id
689e6add5baa2220aab9a52be8eaf486284038ff), port sources src/roomfix (libroomfix), src/java/port/roomfix (HUD,
routing). Device evidence: redmi-note8pro (Redmi Note 8 Pro, Android 11, Mali-G76), runs R003–R010; round 4:
poco-x3-nfc (POCO X3 NFC, Android 16, Adreno 618), R033–R035.

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
- Gestures (rf_touch.c / rf_interact.c): tap, drag, long press, two-finger pinch / pan (the eyes move inside the
  location, rf_view.c), two-finger hold+look.

## Action map (VR column and native point)

| Action | VR (original) | Flat touch | Native point used | Context | Evidence |
|---|---|---|---|---|---|
| Look around | head rotation | one-finger drag on empty space (100° per screen width); in menus a drag never turns the camera (round 2, R2-1), nor without a player, e.g. while main-menu pages change (round 4, R4-8) | head pose node (PoseDataSource / XR head device) | gameplay | R003, R009, R026, R035 |
| Turn / tilt by moving the phone (option) | head rotation | Options → Gyroscope → TILT AND TURN on (sensitivity 25–300 %): the camera turns with the phone, yaw around the real vertical, slow drift faded out | head yaw / pitch (rf_gyro.c; gyroscope + gravity sensors only while on) | gameplay (not menus) | R026 (synthetic sensor input; device on a desk) |
| Move the eyes by moving the phone (option) | move the head | Options → Gyroscope → EYE POSITION on (sensitivity): turning / tilting the phone shifts the eyes sideways / up inside the location's play area (same limits as the two-finger moves, round 5), moving it away / closer leans in / out (linear acceleration, zero-velocity updates; the shift glides back in ~4 s) | head position | gameplay (not menus) | R026 (`push 0.1` → 0.118 m), R036 (0.087 m per 10°, stop at the edge) |
| Stand in the play area | the head stays where the player stands; turning does not move it | round 5 (operator 2026-10-17, replaces round 4's automatic step back): every arrival (teleport, level start, continue) puts the eyes at the location's centre - the location point at eye level; turning the view (swipe, gyroscope, the game's snap turn) never moves them | head position in tracking space (rf_view.c); the snap turn's rig rotation is turned back on the offset | gameplay | R036 (0.000 m after teleports; eye world unchanged through 360° of swipes and snap turns), R037 |
| Step forward / back (lean in, back off to see a puzzle whole) | lean / step | two-finger spread / pinch: along the view direction | head position, kept inside the play area: fade-out radius (MaxPlayerDistanceBeforeFadeOut) minus 0.12 m around the location point, ±0.4 m in height, 0.1 m from walls, solid objects and the game's head-collision volumes (swept sphere), never a layer-2 volume between the head and the location's eye level (the game's own fade-out rules) | gameplay | R010, R036 (stop 0.11 m before a wall volume; in front of the projector) |
| Move the head sideways / up (peek around, reach a rope strand behind another) | move the head | two fingers moving together: sideways along the view's horizontal right, up / down | same limits | gameplay | p063, R036 |
| Where am I in the location (position indicator) | the play area around the VR player | while two fingers move the eyes along an axis and they are 8 cm or more from the centre on it, a track for that axis in the top right corner (forward lane, right bar, up bar): the location's span, the reachable part, the centre and the eyes; a stop flash where a move is held back; fades 0.9 s after the last move; Options → POSITION INDICATOR on / off (round 5) | HudView over native data (rf_view.c g_gauge, RoomFix.nativeGauge) | gameplay | R036, R037 |
| Edge of the play area | the game fades to black, "Please return to the play area", return marker | the eyes stop at the edge; the game's own return marker (ring with an arrow over the location point, glow, embers) is shown over the scene while a move presses against the edge or a head-collision volume, 1.5 s after (with the indicator option) | HeadInsideGeometryHUD.ReturnToLocationMarker activated at the location point while the game's FadeState is Inactive | gameplay | R036 |
| Snap turn | stick left/right | HUD ↶ ↷ (shown while the tutorial asks) | stick X pulse → RwAction 6/7 | tutorial | R004, R009 |
| Teleport | stick forward, aim arc, release | HUD teleport → tap a glowing location marker | TeleportPointer aimed at PlayerLocation, stick release | gameplay | R004, R007, R009 |
| Teleport past a nearer location | aim the arc further | same tap | the arc ends on another location → the game's Player.TeleportTo on the tapped one | gameplay | p063, R017 (RigHandle, PullRopeWheel, Canopic chest, sarcophagus side) |
| Arrival view | body faces location | camera turns to the point a seated VR player sees in the middle (0.6 m along the location's forward, 20° down); at a level's first location to the level's title while it is shown (round 4, R4-12) | head yaw/pitch from PlayerLocation.forward / the Scene_Setting canvas | after teleport, level start | R009, R034, R035 |
| Superzoom (enter/exit miniature) | stick forward at crystals | eyepiece on, teleport mode → tap the crystals; inside: teleport mode → tap the exit portal | TeleportPointer at Superzoom ZoomIn/Portal/ZoomOut trigger (only from the superzoom's MainSceneLocation, goggles worn) | chapters | R019 (hymn board superzoom entered and left; unlocked ahead of the story by the game's SetState) |
| Hub between chapters: artefact into its slot, letter through the eyepiece | carry the artefact, hold the letter up | inventory → drag the artefact onto the slot; hold the letter, turn with a second finger, bring its hole over the wisp | InventoryReceiver; grab + head pose | hub | R018 (scene -> Chapel) |
| Grab and move objects (levers, handles) | grip + arm motion | press on the object, drag | right Controller.Grabber pose; GrabbableCollider math volumes; constraint-aware drag (slider line; rotation: the hand on its circle at the point whose screen position is nearest the finger, from the last angle and within the part's limits - round 4; twist) | gameplay | R005, R009, R010, R034 (locker handle 90° seen almost edge-on) |
| Sliders (drawers, grilles, tabs) | grip + pull | drag along the slide | SliderGrabbable LocalSlideAxis | gameplay, inventory | R009, R010 |
| Knobs / keys (wrist twist) | twist wrist | finger on the knob, out to the side, circle around it (multi-turn) | hand rotation about RotateAxis (TwistControl) | gameplay | R006, R009, R010 |
| Position-driven knobs | turn hand around | same circle gesture | off-axis grab 2 cm from the axis | gameplay | R010 |
| Cranks | circle the arm | circle around the axis, any number of turns | grab point on the circle (K_ROTATION) | gameplay | R010 (2891°), R034 (2898.6°) |
| Lift and swing a bolt over its support | lift the handle, swing, let go | press the handle, drag it towards you, up, back over the support, lift the finger | CombinationGrabbable: hand on the cylinder around the shared rotate/slide axis; of the ray's two hits the one inside the slide limits, past the outline the part rolls on | gameplay | R017 |
| Twist a bar and pull it | twist wrist, pull | press the grip, drag across the bar (past its outline to keep turning), then along it | same cylinder; a grip on the axis itself rolls the bar under the finger | gameplay | R017 |
| Winch with a return spring (many turns at once) | crank continuously | one continuous circle gesture (≈14 turns, 400°/s cap) | K_ROTATION; SpringToLimit returns the crank when released | gameplay | R017 (4000°) |
| Look through a note's hole (eyepiece) | hold the note up to the eye | finger 0 holds the note, finger 1 turns the view, finger 0 brings the hole over the glowing target | grab + head yaw/pitch; the game's own look check | gameplay | R017 |
| Turn a held object in the hand (read a note, tip a jar to pour) | rotate the wrist | second finger next to the holding one drags a virtual ball: circle = roll, across = turn over, up/down = tip | right hand rotation (rf_act_grab_turn) | gameplay | R018 (note turned to its text); pouring itself: debt, Witch chapter "MixFirstSpell" (CauldronIngredientBottle chunks) |
| Look-through mirror / glass at crystals | hold the glass up, align | same as the note hole (hold + turn + align) | grab + head pose | Witch chapter | debt: "RevealTreeWithMirror" |
| Pick up item (proxy) | grip, release | long press, lift | InventoryItemProxy grab/release → inventory | gameplay | R009 |
| Read a picked-up item (note, clipboard, tool) | hold in front of the eyes, turn the wrist | long press: the item comes up to be read, its face (the side the inventory shows) towards the eyes, upright, wholly in view; moving the finger carries it (round 4, R4-4) | grabber pose and rotation (InventoryItemGrabbable.InventorySlotTransform axes) | gameplay | R009, R034 |
| Carry item to a place | carry in hand | drag; item follows the surface under the finger | depth from physics raycast (ignores held item, inventory grid) | gameplay | R010 |
| Inventory open/close | A / X button | HUD inventory | right primary pulse (RwAction 21) → HudManager.ToggleOnOff; grid follows camera pitch; on opening it is framed wholly in view, as large as in VR, in front of anything in the way (round 4, R4-2) | gameplay | R007, R010, R034, R035 |
| Take item from inventory | point finger at slot + grip | press on the slot | PointingFingerTip ray (InventoryHud.RaycastInventory) | inventory | R006, R009 |
| Use item on receiver (keyhole, socket) | carry to receiver, release | drag onto it, lift | receiver magnet (Controller.InventoryPoint probe formula) | gameplay | R006, R009, R010 |
| Grab hand-over (key becomes lock) | regrip | automatic release + re-grab | G_REGRAB | gameplay | R006, R009 |
| Hints | pull ? tab, then hint tab | drag the tabs down; the hint shows while the tab is held (the tab's grab volume sits below the drawn "?": wider magnet, grabbed as an inventory part - round 4, R4-1) | SliderGrabbable "Hints Tab n Grabbable" | inventory | R010, R034, R035 |
| Eyepiece on/off | pull the inventory tab (or PS button) | HUD eyepiece, or drag the tab | GogglesHudManager.Activate/DeactivateWithFade (same as Player.UpdateGogglesInput) / tab RotationGrabbable | gameplay | R010 |
| Hold object + look around | hand holds, head turns | finger 1 holds, finger 2 swipes | grab + head yaw | gameplay | R010 |
| HUD while holding | — | other finger on a HUD button | per-pointer ownership | gameplay | R010 |
| Menu buttons | laser + trigger | tap; the pointer beam is never drawn (round 2, R2-3) | right ray on the camera ray through the finger + trigger; pointer hand-over press; FrontEndPointer.SetLength keeps its line renderers off | main/pause menus | R003, R007, R026 (A/B) |
| Menu in view | turn to the panel | automatic: the camera turns to the current screen's content (UI, meshes, its own scene geometry such as the 3D logo) and widens the vertical FOV a menu needs when the chosen one is narrower (round 2, R2-2); the pause menu gives the view back on close | head yaw / pitch, Camera.fieldOfView in menu context | main/pause menus | R026, R027 |
| Read tutorial boxes / remarks | boxes in front of the head, lazily following it (StayInView) | none: they stay fixed on the screen — instructions at the top, remarks at the bottom — drawn over the scene (round 2, R2-5); letters of a set size in millimetres, every box inside the screen at any aspect (round 4, R4-10/R4-11); two boxes at once stacked (round 4) | StayInView.Update replaced; boxes pinned to the camera pose, own camera on layer 30 | gameplay, loading | R026, R034, R035 |
| Option selectors | laser + trigger on arrows | tap the arrows | UiOptionSelector buttons | options | R008 |
| Back | — | Android Back: previous menu screen / resume / pause (Android 16+: predictive back opted out in the manifest, OnBackInvokedCallback with the same meaning - round 4) | MenuController.TransitionToScreen, left menu (RwAction 22/27) | all | R007, R008, R035 |
| Pause | left menu button | HUD pause, Back | GameStateManager.Pause | gameplay | R007 |
| Posture wizard (VR) | choose sitting/standing | answered automatically (Sitting), never shown (round 4, R4-9) | PostureSelectScreen.SittingPressed; the wizard opened at scale 0 | front end | R007, R035 |
| Hand model (glove) | always visible | shown only while it grips a lever, handle or knob; no empty glove before a grip or after letting go (round 4, R4-7) | HandController.SetVisible with the port's own reason | gameplay | R034 |
| Recenter / height calibration / turn fade | VR settings | hidden | OptionsScreen rows hidden | options | R008 |
| VR controls map (Options: view controls) | controller diagram | gesture card (Java overlay, game language); also once on the first main menu; tap or Back closes | Button.Press of 'Button [Controls]' answered by libroomfix (rf_help.c) -> RoomFix.nativeHelpRequest | front end, pause | R022 |
| Chapter select (stage 8) | none (authored screen unused) | main menu LEVEL SELECT -> chapter list -> tap a chapter | cloned MenuButtons, GameStateManager.LoadLevel with level saves and saving off | front end | R022 |
| Poke-slide panels and buttons | push with the fingertip | press on it and drag / press | Controller.PokeProbe placed on the target for CanBePoked volumes (the game feeds the probe position and lets go when it leaves the volume) | gameplay | R017 (finger-lock panel) |
| Organ keyboard | press keys with the fingers | tap the keys (grab/poke path) | KeyboardVolume | Chapel | debt: "Organ Holo Press Black Key" (after the organ superzoom chain) |
| Lockpick | turn the pick in the lock | knob twist gesture | RotationGrabbable (LockpickPuzzle) | Chapel | debt: "Solve Lockpick Organ" / "Solve Lockpick Clock" |
| Bellows, candles | pump, carry a flame | drag the handle; carry the firelighter to the coals and candles | Slider/Rotation grabbables; carried item | Witch chapter | debt: "PumpBellowsForFirstSpell", "ThreeCandles" |
| Ropes | pull rope hand over hand | downward drags on the rope, one per pull | RopeGrabbable along its LocalSlideAxis | gameplay | R017 (door rope, RopeSystem 2.355) |
| Text input | none in the game | — | — | — | no text entry exists |
| Save / load | autosave, Continue | Continue game / profiles by tap | SaveManager (external files dir) | front end | R009 continue runs |

## Multi-touch rules
- Stable pointer ids; HUD fingers never reach the game; the primary game finger owns grab/look; a second finger
  pinches / pans (when looking) or turns the view (when holding). Lifting the primary ends the gesture; orphan moves
  are ignored until a new down. Window focus loss cancels all fingers (no stuck grip); CANCEL releases grabs.
- Two-finger moves are incremental (each event moves by the change since the last one), so a move held back at the
  edge of the play area comes back as soon as the fingers turn round.

## Remaining work / debts (TOUCH_GAMEPLAY_VERIFIED)
- Full cycle cold start → new game → tutorial → save → restart/resume: done up to the open safe + continue (R009,
  R010); the rest of the tutorial needs puzzle solving, not new controls.
- Egypt front half by touch (R017): door bolts, lever, rope, winch, note through the eyepiece, box, lens,
  eyepiece tab, poke panel; hub → Chapel and turning a held object (R018).
- Superzoom enter/exit by touch (R019).
- Story-placed debts (the route did not reach them; flat paths implemented): Chapel organ keys, lockpick; Witch
  chapter pouring ("MixFirstSpell"), mirror alignment, bellows, candles.
- First-run hint for the gestures: done, gesture card on the first main menu and from Options (stage 8 C3, R022).
