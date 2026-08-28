# ESP-SR 2.4.7 names the registry component explicitly in one place and uses
# the unqualified dependency name in another. Keep one cJSON implementation
# and expose this zero-source compatibility component to all project users.
idf_component_register(REQUIRES espressif__cjson)
