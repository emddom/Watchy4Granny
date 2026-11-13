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
                  if (Menu.Item == GSR_MENU_UTILS_BTN) { InsertHandleShowButtons(); }
                  else if (Menu.Item == GSR_MENU_UTILS_FIND) { InsertHandleFindPhone(); }
                  else if (Menu.Item == GSR_MENU_UTILS_MED) { InsertHandleMedInfo(); }
                  else if (Menu.Item == GSR_MENU_UTILS_WEAT) { InsertHandleWeather(); }
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
                  Menu.Item = roller(Menu.Item - 1, GSR_MENU_UTILS_BTN, GSR_MENU_UTILS_WEAT);
              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item - 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
content = content.replace(case3_utils, case3_utils_replacement)

# 6. Add handleMenuButton Case 4 (Down)
case4_utils = """              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
case4_utils_replacement = """              }else if (Menu.Style == GSR_MENU_INUTILS){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_UTILS_BTN, GSR_MENU_UTILS_WEAT);
              }else if (Menu.Style == GSR_MENU_INTROUBLE){
                  Menu.Item = roller(Menu.Item + 1, GSR_MENU_SCRN, GSR_MENU_UNVS);"""
content = content.replace(case4_utils, case4_utils_replacement)

with open('src/Watchy_GSR.cpp', 'w') as f:
    f.write(content)
