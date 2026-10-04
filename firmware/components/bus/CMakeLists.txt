# bus: the event queue between TinyBar's tasks and the app task. Owner: lead developer.
idf_component_register(SRCS "tb_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES core freertos log)
