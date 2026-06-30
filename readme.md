Calculator Watch
================================================================================

A fully functional calculator built as a Pebble watchapp for the Pebble Time 2
and Pebble Round 2, with a real-time clock display and full touch-screen input.

Platform  : Pebble Time 2 and Pebble Round 2
SDK       : Pebble SDK 6.x
Language  : C
Build     : PebbleCloud
License   : MIT


--------------------------------------------------------------------------------
SCREENSHOTS
--------------------------------------------------------------------------------

Add screenshots to a /screenshots folder in this repository after capturing
them from the PebbleCloud emulator.

  screenshots/time2.png    - Pebble Time 2 (rectangular screen)
  screenshots/round2.png   - Pebble Round 2 (round screen)


--------------------------------------------------------------------------------
FEATURES
--------------------------------------------------------------------------------

  - Full calculator with addition, subtraction, multiplication, and division
  - Percent key with iOS-style percentage logic
  - Sign toggle to flip positive and negative instantly
  - Chained operations evaluated left to right
  - Error handling with Err:Div0 message on division by zero
  - Live clock shown in the display panel, updates every minute
  - Operator badge shows the pending operator while entering the right operand
  - Full touch input with every button responding to tap gestures
  - Visual press feedback with buttons inverting colours when held
  - Adaptive layout that auto-scales to both rectangular and round screens
  - Optional haptic feedback available with a one-line change


--------------------------------------------------------------------------------
LAYOUT
--------------------------------------------------------------------------------

  +------------------------------+
  | 12:45                    +   |  clock and pending operator badge
  |                      12345   |  calculator display
  +-------+-------+-------+------+
  |  AC   |  +/-  |   %   |  /   |  row 0 - function keys
  +-------+-------+-------+------+
  |   7   |   8   |   9   |  x   |  row 1
  +-------+-------+-------+------+
  |   4   |   5   |   6   |  -   |  row 2
  +-------+-------+-------+------+
  |   1   |   2   |   3   |  +   |  row 3
  +---------------+-------+------+
  |       0       |   .   |  =   |  row 4
  +---------------+-------+------+

The layout is calculated at runtime from the device's actual screen bounds so
the same binary works correctly on both devices without any conditional
compilation.


--------------------------------------------------------------------------------
PROJECT STRUCTURE
--------------------------------------------------------------------------------

  calculator-watch/
  |
  +-- src/
  |   +-- main.c          Full calculator engine, UI, and touch handling
  |
  +-- screenshots/
  |   +-- time2.png        Screenshot on Pebble Time 2  (add your own)
  |   +-- round2.png       Screenshot on Pebble Round 2 (add your own)
  |
  +-- package.json         App manifest for PebbleCloud and Pebble SDK 6
  +-- README.md            This file
  +-- LICENSE              MIT License


--------------------------------------------------------------------------------
BUILDING
--------------------------------------------------------------------------------

This project is designed to be built with PebbleCloud, Rebble's browser-based
IDE and build environment.


Prerequisites
-------------

  Rebble account      https://auth.rebble.io
  PebbleCloud access  https://pebblecloud.com
  Pebble app          Required on your phone to sideload the compiled .pbw file


Build Steps
-----------

  1. Log in to PebbleCloud
  2. Create a new project and select Watchapp
  3. In the file tree, open src/main.c and replace its contents with the
     contents of this repository's src/main.c
  4. Open package.json and replace its contents with this repository's
     package.json
  5. Click the UUID field in package.json and select Generate UUID
  6. Verify that targetPlatforms matches your device:
       time2   for Pebble Time 2
       round2  for Pebble Round 2
  7. Click the Build button
  8. Install to your watch via QR code scan or USB sideload


Build Troubleshooting
---------------------

If the build fails on the touch API, the PebbleCloud SDK version may use a
slightly different function signature. Four alternatives are documented at the
bottom of src/main.c.

  Option A (default)
    Signature         (GPoint, TouchPhase, void*)
    Registration      window_set_touch_handler()

  Option B
    Signature         (GPoint, bool, void*)
    Registration      window_set_touch_handler()

  Option C
    Signature         (TouchEvent, void*)
    Registration      window_set_touch_event_handler()

  Option D
    Type              Global service
    Registration      touch_event_service_subscribe()

Switch to the appropriate option in both the handler function signature and the
registration call inside window_load().


--------------------------------------------------------------------------------
CUSTOMISATION
--------------------------------------------------------------------------------

All tweaks are made in src/main.c unless otherwise noted.


Make it a permanent watch face
-------------------------------

In package.json, change:

  "watchface": true

This removes the back-button exit and allows the watch to display the app
automatically on wrist raise.


Add haptic feedback on every button press
------------------------------------------

Inside btn_fire(), before the switch statement, add:

  vibes_short_pulse();


Switch to 12-hour clock format
-------------------------------

In draw_display(), replace the snprintf call with:

  snprintf(ts, sizeof(ts), "%d:%02d%s",
           (tm_p->tm_hour % 12) ? (tm_p->tm_hour % 12) : 12,
           tm_p->tm_min,
           tm_p->tm_hour < 12 ? "a" : "p");


Map the physical SELECT button to AC (All Clear)
-------------------------------------------------

In window_load(), after the touch registration line, add:

  window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler)c_ac);


Change button colours
----------------------

In the D[] table inside btns_init(), each row follows this format:

  { col, row, colspan, BID, label, background_color, foreground_color }

Example - change the equals button to orange:

  {3, 4, 1, B_EQ, "=", GColorOrange, GColorWhite},


Change the display number size threshold
-----------------------------------------

In draw_display(), edit the font-selection block:

  if      (num_h >= 24 && len <=  9) { use GOTHIC_24_BOLD }
  else if (num_h >= 18 && len <= 12) { use GOTHIC_18_BOLD }
  else                               { use GOTHIC_14_BOLD }


--------------------------------------------------------------------------------
CALCULATOR LOGIC REFERENCE
--------------------------------------------------------------------------------

  Key         Behaviour
  ----------  -----------------------------------------------------------------
  0 through 9 Appends digit to current entry. Starts fresh after = or operator.
  .           Appends decimal point. Ignored if one is already present.
  AC          Clears all state and resets display to 0.
  +/-         Negates the current displayed value.
  %           Divides by 100. If a pending + or - exists, computes
              mem * value / 100 following iOS-style percentage logic.
  + - x /     Stores current value and operator. Chains if a prior result
              is pending.
  =           Evaluates the pending operation and shows the result.
  divide by 0 Shows Err:Div0. All keys disabled until AC is pressed.


Chaining example:

  Press 3, +, 4, =  -->  displays 7
  Press +, 2, =     -->  displays 9 (chains from the previous result)


--------------------------------------------------------------------------------
ARCHITECTURE
--------------------------------------------------------------------------------

  Component              Location      Responsibility
  ---------------------  ------------  -----------------------------------------
  geo_init()             src/main.c    Reads actual screen bounds and derives
                                       DISP_H, BTN_W, and BTN_H at runtime
  c_* functions          src/main.c    Pure calculator engine with no UI
                                       dependency
  btns_init()            src/main.c    Builds GRect for each button from a
                                       compact data table
  touch_handler()        src/main.c    Sets held state on press and fires the
                                       action on release
  canvas_update_proc()   src/main.c    Single-pass draw: display panel first,
                                       then the button grid
  tick_handler()         src/main.c    Marks the layer dirty every minute to
                                       refresh the clock display

The calculator engine (all c_* functions) has no dependency on any Pebble SDK
types and can be extracted and unit-tested independently.


--------------------------------------------------------------------------------
COMPATIBILITY
--------------------------------------------------------------------------------

  Device                  Screen shape   Resolution   Status
  ----------------------  -------------  -----------  --------------------------
  Pebble Time 2           Rectangular    144 x 168    Supported
  Pebble Round 2          Round          180 x 180    Supported

This app requires a touch-enabled Pebble device. Older models without a touch
screen cannot use touch input and are not supported.


--------------------------------------------------------------------------------
CONTRIBUTING
--------------------------------------------------------------------------------

Contributions, issues, and feature requests are welcome.

  1. Fork the repository
  2. Create a feature branch:   git checkout -b feature/my-feature
  3. Commit your changes:       git commit -m "Add my feature"
  4. Push to the branch:        git push origin feature/my-feature
  5. Open a Pull Request


Ideas for future contributions:

  - Memory functions (M+, M-, MR, MC)
  - Calculation history log with scroll support in the display
  - Scientific mode revealed by landscape orientation or button hold
  - Configurable colour themes via the Pebble phone app
  - Unit tests for the c_* calculator engine functions
  - Animated digit transitions on result display


--------------------------------------------------------------------------------
LICENSE
--------------------------------------------------------------------------------

This project is licensed under the MIT License.
See the LICENSE file for full details.


--------------------------------------------------------------------------------
ACKNOWLEDGEMENTS
--------------------------------------------------------------------------------

  Rebble
    For keeping the Pebble ecosystem alive and maintaining PebbleCloud.
    https://rebble.io

  Pebble Developer Documentation
    SDK reference and guides maintained by the Rebble community.
    https://developer.rebble.io/developer.pebble.com/index.html

  Pebble Community on Discord
    For ongoing SDK support and answers.
    https://rebble.io/discord


--------------------------------------------------------------------------------
CONTACT
--------------------------------------------------------------------------------

Have a question or found a bug? Open an issue on this repository.

================================================================================