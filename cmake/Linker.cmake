macro(Darkest_Dungeon_Modloader_configure_linker project_name)
  set(Darkest_Dungeon_Modloader_USER_LINKER_OPTION
    "DEFAULT"
      CACHE STRING "Linker to be used")
    set(Darkest_Dungeon_Modloader_USER_LINKER_OPTION_VALUES "DEFAULT" "SYSTEM" "LLD" "GOLD" "BFD" "MOLD" "SOLD" "APPLE_CLASSIC" "MSVC")
  set_property(CACHE Darkest_Dungeon_Modloader_USER_LINKER_OPTION PROPERTY STRINGS ${Darkest_Dungeon_Modloader_USER_LINKER_OPTION_VALUES})
  list(
    FIND
    Darkest_Dungeon_Modloader_USER_LINKER_OPTION_VALUES
    ${Darkest_Dungeon_Modloader_USER_LINKER_OPTION}
    Darkest_Dungeon_Modloader_USER_LINKER_OPTION_INDEX)

  if(${Darkest_Dungeon_Modloader_USER_LINKER_OPTION_INDEX} EQUAL -1)
    message(
      STATUS
        "Using custom linker: '${Darkest_Dungeon_Modloader_USER_LINKER_OPTION}', explicitly supported entries are ${Darkest_Dungeon_Modloader_USER_LINKER_OPTION_VALUES}")
  endif()

  set_target_properties(${project_name} PROPERTIES LINKER_TYPE "${Darkest_Dungeon_Modloader_USER_LINKER_OPTION}")
endmacro()
