#ifndef __APP_FLASHDB_H__
#define __APP_FLASHDB_H__

// 暴露给 main.c 调用的统一初始化函数
int app_flashdb_init(void);

// 如果主业务需要运行示例，也可以暴露出来
void app_flashdb_run_samples(void);

#endif /* __APP_FLASHDB_H__ */