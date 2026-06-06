file(GLOB SRC CONFIGURE_DEPENDS *.cpp)

set(TARGET back-tester-sim)
add_library(${TARGET} STATIC ${SRC})
target_link_libraries(${TARGET} PUBLIC back-tester-lob back-tester-common back-tester-ingestion)
