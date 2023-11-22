#include "AppRuntime.h"
#include "WorkQueue.h"
#include <exception>
#include <sstream>
#include <functional>
#include <napi/js_native_api.h>
#include <napi/js_runtime_api.h>

namespace Babylon
{
    void ScheduleTaskCallback(void *task_runner_data, void *task_data, jsr_task_run_cb task_run_cb, jsr_data_delete_cb task_release_cb, void *deleter_data)
    {
        WorkQueue* worker = reinterpret_cast<WorkQueue*>(task_runner_data);

        worker->Append([task = std::move(task_run_cb), task_data, task_release_cb, deleter_data](Napi::Env)
        { 
            task(task_data);
            task_release_cb(task_data, deleter_data);
        });
    }

    void __cdecl v8TaskRunnerReleaseCb(void *, void *)
    {

    }

    void AppRuntime::RunEnvironmentTier(const char*)
    {
        napi_status result{napi_status::napi_ok};

        jsr_config config;
        result = jsr_create_config(&config);
        assert(result == napi_status::napi_ok);

        result = jsr_config_enable_inspector(config, true);
        assert(result == napi_status::napi_ok);

        result = jsr_config_set_inspector_break_on_start(config, false);
        assert(result == napi_status::napi_ok);

        result = jsr_config_set_task_runner(config, this->m_workQueue.get(), &ScheduleTaskCallback, v8TaskRunnerReleaseCb, nullptr);
        assert(result == napi_status::napi_ok);

        jsr_runtime runtime;
        result = jsr_create_runtime(config, &runtime);
        assert(result == napi_status::napi_ok);

        result = jsr_delete_config(config);
        assert(result == napi_status::napi_ok);

        napi_env _env{};
        result = jsr_runtime_get_node_api_env(runtime, &_env);
        assert(result == napi_status::napi_ok);

        Napi::Env env = Napi::Env(_env);

        jsr_napi_env_scope scope;
        result = jsr_open_napi_env_scope(_env, &scope);
        assert(result == napi_status::napi_ok);

        Run(env);

        result = jsr_close_napi_env_scope(_env, scope);
        assert(result == napi_status::napi_ok);

        // TODO: jsr_delete_runtime needs to be called somewhere, but not here.
        // See https://github.com/microsoft/v8-jsi/blob/559e188e3d13a97b75372119fde2b4a53a1addbc/src/testmain.cpp#L41
        //result = jsr_delete_runtime(runtime);
        //assert(result == napi_status::napi_ok);
    }
}