/*
 * tb_bus.c: the app task's event queue and tb_bus_exec(). Owner: lead developer.
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "tb_bus.h"

static const char *TAG = "bus";

static QueueHandle_t s_queue;
static TaskHandle_t s_app_task;
static volatile uint32_t s_dropped;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef enum { JOB_QUEUED = 0, JOB_RUNNING, JOB_DONE, JOB_CANCELED } job_state_t;

/* One tb_bus_exec() call. Heap-allocated; freed by whichever side finishes with it last. */
typedef struct {
    tb_exec_fn fn;
    void *ctx;
    SemaphoreHandle_t done;
    job_state_t state;
    uint8_t refs;
} job_t;

static void job_release(job_t *j)
{
    bool last;
    taskENTER_CRITICAL(&s_lock);
    last = --j->refs == 0;
    taskEXIT_CRITICAL(&s_lock);
    if (last) {
        vSemaphoreDelete(j->done);
        free(j);
    }
}

void tb_bus_init(void)
{
    if (!s_queue) s_queue = xQueueCreate(TB_BUS_DEPTH, sizeof(tb_event_t));
    configASSERT(s_queue);
}

void tb_bus_set_app_task(void)
{
    s_app_task = xTaskGetCurrentTaskHandle();
}

bool tb_bus_post(const tb_event_t *ev)
{
    if (s_queue && xQueueSend(s_queue, ev, 0) == pdTRUE) return true;
    s_dropped++;
    return false;
}

bool tb_bus_post_isr(const tb_event_t *ev)
{
    BaseType_t woken = pdFALSE;
    bool ok = s_queue && xQueueSendFromISR(s_queue, ev, &woken) == pdTRUE;
    if (!ok) s_dropped++;
    portYIELD_FROM_ISR(woken);
    return ok;
}

bool tb_bus_post_kind(tb_ev_kind_t kind, int32_t arg)
{
    tb_event_t ev = {.kind = kind};
    ev.u.i32 = arg;
    return tb_bus_post(&ev);
}

bool tb_bus_notify(const char *text)
{
    tb_event_t ev = {.kind = TB_EV_NOTIFY};
    strncpy(ev.u.text, text, sizeof(ev.u.text) - 1);
    return tb_bus_post(&ev);
}

bool tb_bus_exec(tb_exec_fn fn, void *ctx, uint32_t timeout_ms)
{
    if (s_app_task && xTaskGetCurrentTaskHandle() == s_app_task) {
        fn(ctx);
        return true;
    }
    job_t *j = calloc(1, sizeof(*j));
    if (!j) return false;
    j->fn = fn;
    j->ctx = ctx;
    j->done = xSemaphoreCreateBinary();
    j->state = JOB_QUEUED;
    j->refs = 2;    /* the caller and the app task */
    if (!j->done) {
        free(j);
        return false;
    }
    tb_event_t ev = {.kind = TB_EV_EXEC};
    ev.u.exec.fn = fn;
    ev.u.exec.ctx = ctx;
    ev.u.exec.done = j;
    if (!tb_bus_post(&ev)) {
        vSemaphoreDelete(j->done);
        free(j);
        return false;
    }
    bool ran = false;
    if (xSemaphoreTake(j->done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        ran = true;
    } else {
        bool cancel;
        taskENTER_CRITICAL(&s_lock);
        cancel = j->state == JOB_QUEUED;
        if (cancel) j->state = JOB_CANCELED;
        taskEXIT_CRITICAL(&s_lock);
        if (!cancel) {
            /* Already running (or just finished): wait for it, since it may be using ctx. */
            xSemaphoreTake(j->done, portMAX_DELAY);
            ran = true;
        } else {
            ESP_LOGW(TAG, "exec timed out after %u ms", (unsigned)timeout_ms);
        }
    }
    job_release(j);
    return ran;
}

static void run_job(job_t *j)
{
    bool run;
    taskENTER_CRITICAL(&s_lock);
    run = j->state == JOB_QUEUED;
    if (run) j->state = JOB_RUNNING;
    taskEXIT_CRITICAL(&s_lock);
    if (run) {
        j->fn(j->ctx);
        taskENTER_CRITICAL(&s_lock);
        j->state = JOB_DONE;
        taskEXIT_CRITICAL(&s_lock);
        xSemaphoreGive(j->done);
    }
    job_release(j);
}

bool tb_bus_receive(tb_event_t *ev, uint32_t wait_ms)
{
    if (xQueueReceive(s_queue, ev, pdMS_TO_TICKS(wait_ms)) != pdTRUE) return false;
    if (ev->kind == TB_EV_EXEC) {
        /* Run the job here, and hand the caller an empty event, so each job counts against the caller's per-loop
         * budget (a stream of requests can't keep the app task from drawing and feeding the watchdog). */
        run_job((job_t *)ev->u.exec.done);
        ev->kind = TB_EV_NONE;
    }
    return true;
}

uint32_t tb_bus_dropped(void)
{
    return s_dropped;
}
