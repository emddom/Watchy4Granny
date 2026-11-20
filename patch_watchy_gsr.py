import re

with open('src/Watchy_GSR.cpp', 'r') as f:
    content = f.read()

# 1. Update roller for INNORMAL Menu
content = content.replace('Menu.Item - 1, GSR_MENU_ALARMS, GSR_MENU_OPTIONS', 'Menu.Item - 1, GSR_MENU_ALARMS, GSR_MENU_UTILS')
content = content.replace('Menu.Item - 1, GSR_MENU_STEPS, GSR_MENU_OPTIONS', 'Menu.Item - 1, GSR_MENU_STEPS, GSR_MENU_UTILS')
content = content.replace('Menu.Item + 1, GSR_MENU_ALARMS, GSR_MENU_OPTIONS', 'Menu.Item + 1, GSR_MENU_ALARMS, GSR_MENU_UTILS')
content = content.replace('Menu.Item + 1, GSR_MENU_STEPS, GSR_MENU_OPTIONS', 'Menu.Item + 1, GSR_MENU_STEPS, GSR_MENU_UTILS')

# 2. Add UTILS to drawMenu
draw_menu_utils = """        case GSR_MENU_UTILS:
            O = "Utilities";
            break;
        case GSR_MENU_UTILS_BTN:
            O = "Show Buttons";
            break;
        case GSR_MENU_UTILS_FIND:
            O = "Find Phone";
            break;
        case GSR_MENU_UTILS_MED:
            O = "Med Info";
            break;
        case GSR_MENU_UTILS_WEAT:
            O = "Weather";
            break;
        case GSR_MENU_UTILS_BLE:
            O = "BLE Remote";
            break;
        case GSR_MENU_UTILS_WIFI:
            O = "Watchy Connect";
            break;
"""
content = content.replace('case GSR_MENU_OPTIONS:', draw_menu_utils + '        case GSR_MENU_OPTIONS:')

# 3. Add handleMenuButton Case 1 (Enter)
case1_utils = """              if (Menu.Item == GSR_MENU_OPTIONS && Menu.SubItem == 0){  // Options"""
case1_utils_replacement = """              if (Menu.Item == GSR_MENU_UTILS && Menu.SubItem == 0) {
                  Menu.Item = GSR_MENU_UTILS_BTN;
                  Menu.Style = GSR_MENU_INUTILS;
                  Menu.SubItem = 0;
                  Menu.SubSubItem = 0;
                  DoHaptic = true;
                  UpdateDisp = true;
                  SetTurbo();
              }else if (Menu.Style == GSR_MENU_INUTILS) {
                  if (Menu.Item == GSR_MENU_UTILS_BTN) { GuiMode = GSR_WATCHON; InsertHandleShowButtons(); UpdateDisp = true; SetTurbo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_FIND) { GuiMode = GSR_WATCHON; InsertHandleFindPhone(); UpdateDisp = true; SetTurbo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_MED) { GuiMode = GSR_WATCHON; InsertHandleMedInfo(); UpdateDisp = true; SetTurbo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_WEAT) { GuiMode = GSR_WATCHON; InsertHandleWeather(); UpdateDisp = true; SetTurbo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_BLE) { GuiMode = GSR_WATCHON; InsertHandleBleRemote(); UpdateDisp = true; SetTurbo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_WIFI) {
                      Menu.Item = GSR_MENU_WIFI;
                      Menu.Style = GSR_MENU_INOPTIONS;
                      Menu.SubItem = 1;
                      WatchyAPOn = true;
                      ResetEndOTA();
                      DoHaptic = true;
                      UpdateDisp = true;
                      SetTurbo();
                  }
              }else if (Menu.Item == GSR_MENU_OPTIONS && Menu.SubItem == 0){  // Options"""
content = content.replace(case1_utils, case1_utils_replacement)

# 4. Add handleMenuButton Case 2 (Back)
case2_utils = """          }else if (Menu.Style == GSR_MENU_INOPTIONS){"""
case2_utils_replacement = """          }else if (Menu.Style == GSR_MENU_INUTILS){
              Menu.SubItem = 0;
              Menu.SubSubItem = 0;
              Menu.Item=GSR_MENU_UTILS;
              Menu.Style=GSR_MENU_INNORMAL;
              DoHaptic = true;
              UpdateDisp = true;  // Quick Update.
              SetTurbo();
          }else if (Menu.Style == GSR_MENU_INOPTIONS){"""
content = content.replace(case2_utils, case2_utils_replacement)

# 5. Add handleMenuButton Case 3 (Up)
case3_utils = """              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item - 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
case3_utils_replacement = """              }else if (Menu.Style == GSR_MENU_INUTILS){
                  Menu.Item = roller(Menu.Item - 1, GSR_MENU_UTILS_BTN, GSR_MENU_UTILS_WIFI);
              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item - 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
content = content.replace(case3_utils, case3_utils_replacement)

# 6. Add handleMenuButton Case 4 (Down)
case4_utils = """              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
case4_utils_replacement = """              }else if (Menu.Style == GSR_MENU_INUTILS){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_UTILS_BTN, GSR_MENU_UTILS_WIFI);
              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
content = content.replace(case4_utils, case4_utils_replacement)

# 7. Add InsertAddWeb() hook before server.begin()
if 'InsertAddWeb();' not in content:
    content = content.replace('    RefreshCPU(GSR_CPUMAX);\n    server.begin();', '    InsertAddWeb();\n    RefreshCPU(GSR_CPUMAX);\n    server.begin();')

# 8. SafeToDraw - do not blank screen when WatchyAPOn is true
content = content.replace(
    'bool WatchyGSR::SafeToDraw() { return (!(OTAUpdate || WatchyAPOn || (Menu.Item == GSR_MENU_TOFF && Menu.SubItem > 0 && Menu.SubItem < 6))); }',
    'bool WatchyGSR::SafeToDraw() { return (!(OTAUpdate || (Menu.Item == GSR_MENU_TOFF && Menu.SubItem > 0 && Menu.SubItem < 6))); }'
)

# 9. Add stopWatchyAP() and isWatchyAPOn() implementations
stop_ap_code = """
void WatchyGSR::stopWatchyAP() {
    if (WatchyAPOn) {
        server.stop();
        endWiFi();
        setStatus("");
        WatchyAPOn = false;
        OTAEnd = false;
        ResetEndOTA();
        UpdateDisp = true;
        Updates.Full = true;
    }
}

bool WatchyGSR::isWatchyAPOn() const {
    return WatchyAPOn;
}
"""
if 'void WatchyGSR::stopWatchyAP()' not in content:
    content = content.replace(
        'bool WatchyGSR::InGame() { return (GuiMode == GSR_GAMEON); }',
        'bool WatchyGSR::InGame() { return (GuiMode == GSR_GAMEON); }' + stop_ap_code
    )

# 10. Elevation field in Weatherinfo
if 'float Elevation;' not in content:
    content = content.replace(
        'float WindGust;       /* "wind" "gust" from weather info */\n   } Weather;',
        'float WindGust;       /* "wind" "gust" from weather info */\n     float Elevation;      /* "elevation" from weather info */\n   } Weather;'
    )

# 11. Battery status position alignment (BATTy = 193)
content = content.replace('Design.Status.BATTy = 178;', 'Design.Status.BATTy = 193;')

# 12. Clear RTC alarm on EXT1 wakeup (button/BMA) as well
ext1_target = 'case ESP_SLEEP_WAKEUP_EXT1: //button Press, BMA\\n            adcPins();\\n            userWakeup = true;\\n            break;'
ext1_repl = 'case ESP_SLEEP_WAKEUP_EXT1: //button Press, BMA\\n            adcPins();\\n            userWakeup = true;\\n#ifndef SMALL_RTC3_H\\n            SRTC.clearAlarm();\\n#endif\\n            break;'
if 'case ESP_SLEEP_WAKEUP_EXT1: //button Press, BMA\n            adcPins();\n            userWakeup = true;\n#ifndef SMALL_RTC3_H\n            SRTC.clearAlarm();' not in content:
    content = content.replace(
        'case ESP_SLEEP_WAKEUP_EXT1: //button Press, BMA\n            adcPins();\n            userWakeup = true;\n            break;',
        'case ESP_SLEEP_WAKEUP_EXT1: //button Press, BMA\n            adcPins();\n            userWakeup = true;\n#ifndef SMALL_RTC3_H\n            SRTC.clearAlarm();\n#endif\n            break;'
    )

# 13. Always call nextMinuteWake() in deepSleep() to avoid 15/30 min sleep breaks
content = content.replace('if (DM) SRTC.atMinuteWake(N); else SRTC.nextMinuteWake();', 'SRTC.nextMinuteWake();')

# 14. Always return true in Showing() when on main watch face
if 'if (GuiMode == GSR_WATCHON) return true;' not in content:
    content = content.replace(
        'bool WatchyGSR::Showing() {\\n    bool B = (Updates.Full',
        'bool WatchyGSR::Showing() {\\n    if (GuiMode == GSR_WATCHON) return true;\\n    bool B = (Updates.Full'
    )
    content = content.replace(
        'bool WatchyGSR::Showing() {\n    bool B = (Updates.Full',
        'bool WatchyGSR::Showing() {\n    if (GuiMode == GSR_WATCHON) return true;\n    bool B = (Updates.Full'
    )

with open('src/Watchy_GSR.cpp', 'w') as f:
    f.write(content)
