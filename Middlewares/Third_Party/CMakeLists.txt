# Middlewares/Third_Party/CMakeLists.txt

add_library(flashdb_stack STATIC)

# 1. 包含路径 (根据 fal 仓库实际结构更新)
target_include_directories(flashdb_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ports         # 必须放在首位，覆盖子模块默认配置
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/inc
)

# 2. 源文件收集
target_sources(flashdb_stack PRIVATE
    # SFUD
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/src/sfud.c
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/src/sfud_sfdp.c
    
    # FAL
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal_partition.c
    
    # FlashDB
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_kvdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_tsdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_utils.c
    
    # 本地 ports 适配文件
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/sfud_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/fal_flash_sfud_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/app_flashdb.c
)

# 3. 直接链接 CubeMX 生成的 stm32cubemx 目标
# 这样 flashdb_stack 会自动继承 STM32 HAL 库的头文件路径（如 stm32f4xx_hal.h）和芯片宏定义
target_link_libraries(flashdb_stack PRIVATE
    stm32cubemx
)