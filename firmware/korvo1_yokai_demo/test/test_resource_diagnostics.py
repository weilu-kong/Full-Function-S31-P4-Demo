#!/usr/bin/env python3
"""Execute the actual bounded resource sampler against minimal SDK boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/app_health.c').read_text()
assert 'void app_health_log_resources(void)' in source, 'optional resource sampler is missing'
start = source.index('#if APP_HEALTH_RUNTIME_ENABLED\n')
end = source.index('#endif /* APP_HEALTH_RUNTIME_ENABLED */', start)
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#define configNUMBER_OF_CORES 2
#define configTASKLIST_INCLUDE_COREID 1
typedef void *TaskHandle_t;
typedef unsigned UBaseType_t;
typedef uint64_t configRUN_TIME_COUNTER_TYPE;
typedef struct {TaskHandle_t xHandle;const char *pcTaskName;unsigned xTaskNumber;
    int eCurrentState;unsigned uxCurrentPriority,uxBasePriority;uint64_t ulRunTimeCounter;
    void *pxStackBase;uint32_t usStackHighWaterMark;int xCoreID;} TaskStatus_t;
static TaskStatus_t input[41];
static unsigned count, snapshots;
static bool capacity_race;
static uint64_t clock_us;
static char logs[20000];
static const char *TAG="test";
static void log_line(const char *tag,const char *fmt,...) {
    (void)tag;size_t used=strlen(logs);va_list ap;va_start(ap,fmt);
    vsnprintf(logs+used,sizeof(logs)-used,fmt,ap);va_end(ap);
    strcat(logs,"\n");
}
#define ESP_LOGI log_line
#define ESP_LOGW log_line
static TaskHandle_t xTaskGetIdleTaskHandleForCore(int core) {return (void*)(uintptr_t)(core+1);}
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return (void*)3;}
static UBaseType_t uxTaskGetNumberOfTasks(void) {return capacity_race ? 40 : count;}
static UBaseType_t uxTaskGetSystemState(TaskStatus_t *out,unsigned capacity,uint64_t *total) {
    ++snapshots;if(count>capacity)return 0;
    memcpy(out,input,count*sizeof(*out));*total=clock_us;return count;
}
'''
code += source[start + len('#if APP_HEALTH_RUNTIME_ENABLED\n'):end]
code += r'''
static void task(unsigned slot,unsigned id,uint64_t runtime,int core) {
    input[slot]=(TaskStatus_t){.xHandle=(void*)(uintptr_t)(slot+1),
        .pcTaskName=(void*)1, /* invalid on purpose: sampler must never dereference */
        .xTaskNumber=id,.ulRunTimeCounter=runtime,.usStackHighWaterMark=1234,.xCoreID=core};
}
static void sample(uint64_t now) {clock_us=now;logs[0]=0;app_health_log_resources();}
int main(void) {
    count=3;task(0,1,10,0);task(1,2,20,1);task(2,3,0,-1);
    sample(100);assert(strstr(logs,"baseline") && !strstr(logs,"busy="));
    task(0,1,510,0);task(1,2,270,1);task(2,3,250,-1);
    sample(1100);
    assert(strstr(logs,"core=0 idle=50.0% busy=50.0%"));
    assert(strstr(logs,"core=1 idle=25.0% busy=75.0%"));
    assert(strstr(logs,"id=3") && strstr(logs,"cpu_total=12.5%") && strstr(logs,"stack_min_bytes=1234"));
    /* Ready-list order changes; matching must use identity rather than slot. */
    TaskStatus_t swap=input[0];input[0]=input[2];input[2]=swap;
    input[0].ulRunTimeCounter+=250;input[1].ulRunTimeCounter+=250;input[2].ulRunTimeCounter+=500;
    sample(2100);assert(strstr(logs,"core=0 idle=50.0% busy=50.0%"));
    swap=input[0];input[0]=input[2];input[2]=swap;
    /* Reused handle is a new task: do not subtract its predecessor's counter. */
    task(2,4,100,-1);sample(3100);assert(strstr(logs,"id=4") && strstr(logs,"new/reset"));
    task(2,4,300,-1);sample(4100);assert(strstr(logs,"cpu_total=10.0%"));
    /* Deleted task disappears cleanly. */
    count=2;sample(5100);assert(!strstr(logs,"id=4"));
    count=40;for(unsigned i=2;i<count;++i)task(i,i+1,0,-1);
    sample(6100);assert(strstr(logs,"id=40"));
    count=41;unsigned before=snapshots;sample(7100);
    assert(strstr(logs,"capacity=40") && snapshots==before);
    capacity_race=true;sample(8100);assert(strstr(logs,"unavailable") && snapshots==before+1);
    capacity_race=false;
    count=2;sample(9100);assert(strstr(logs,"baseline"));
    /* Zero interval and resetting timer invalidate a baseline. */
    sample(9100);assert(strstr(logs,"baseline") && !strstr(logs,"busy="));
    sample(10);assert(strstr(logs,"baseline"));
    task(0,1,1,0);sample(1010);assert(strstr(logs,"core=0 idle=unknown") && strstr(logs,"new/reset"));
    /* Snapshot API rejects a capacity race by returning zero. */
    count=0;sample(2010);assert(strstr(logs,"unavailable"));
    count=2;task(0,1,UINT64_MAX-499,0);task(1,2,UINT64_MAX-249,1);
    sample(UINT64_MAX-999);assert(strstr(logs,"baseline"));
    task(0,1,0,0);task(1,2,0,1);sample(0);
    assert(strstr(logs,"core=0 idle=50.0% busy=50.0%"));
    assert(strstr(logs,"core=1 idle=25.0% busy=75.0%"));
    puts("resource diagnostics PASS (baseline, SMP, churn, overflow, resets, rollover)");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cfile, binary = Path(tmp) / 'check.c', Path(tmp) / 'check'
    cfile.write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
