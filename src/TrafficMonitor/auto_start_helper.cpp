#include "stdafx.h"
#include "auto_start_helper.h"

#include <Lmcons.h>

#include <comdef.h>
#include <taskschd.h>
# pragma comment(lib, "taskschd.lib")

// Helper macros from wix.
// TODO: use "s" and "..." parameters to report errors from these functions.
#define ExitOnFailure(x, s, ...) \
    if (FAILED(x))               \
    {                            \
        goto LExit;              \
    }
#define ExitWithLastError(x, s, ...)       \
    {                                      \
        DWORD Dutil_er = ::GetLastError(); \
        x = HRESULT_FROM_WIN32(Dutil_er);  \
        if (!FAILED(x))                    \
        {                                  \
            x = E_FAIL;                    \
        }                                  \
        goto LExit;                        \
    }
#define ExitFunction() \
    {                  \
        goto LExit;    \
    }

const DWORD USERNAME_DOMAIN_LEN = DNLEN + UNLEN + 2; // Domain Name + '\' + User Name + '\0'
const DWORD USERNAME_LEN = UNLEN + 1; // User Name + '\0'

bool create_auto_start_task_for_this_user(bool runElevated)
{
    HRESULT hr = S_OK;
    DWORD executableLength = 0;

    WCHAR username_domain[USERNAME_DOMAIN_LEN];
    WCHAR username[USERNAME_LEN];

    std::wstring wstrTaskName;

    ITaskService* pService = NULL;
    ITaskFolder* pTaskFolder = NULL;
    ITaskDefinition* pTask = NULL;
    IRegistrationInfo* pRegInfo = NULL;
    ITaskSettings* pSettings = NULL;
    ITriggerCollection* pTriggerCollection = NULL;
    IRegisteredTask* pRegisteredTask = NULL;

    // ------------------------------------------------------
    // Get the Domain/Username for the trigger.
    if (!GetEnvironmentVariable(L"USERNAME", username, USERNAME_LEN))
    {
        ExitWithLastError(hr, "Getting username failed: %x", hr);
    }
    if (!GetEnvironmentVariable(L"USERDOMAIN", username_domain, USERNAME_DOMAIN_LEN))
    {
        ExitWithLastError(hr, "Getting the user's domain failed: %x", hr);
    }
    wcscat_s(username_domain, L"\\");
    wcscat_s(username_domain, username);

    // Task Name.
    wstrTaskName = L"Autorun for ";
    wstrTaskName += username;

    // Get the executable path passed to the custom action.
    WCHAR wszExecutablePath[MAX_PATH];
    executableLength = GetModuleFileName(NULL, wszExecutablePath, MAX_PATH);
    if (executableLength == 0 || executableLength >= MAX_PATH)
    {
        hr = HRESULT_FROM_WIN32(executableLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
        ExitFunction();
    }

    // ------------------------------------------------------
    // Create an instance of the Task Service.
    hr = CoCreateInstance(CLSID_TaskScheduler,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_ITaskService,
                          (void**)&pService);
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    // Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    // ------------------------------------------------------
    // Get the TrafficMonitor task folder. Creates it if it doesn't exist.
    hr = pService->GetFolder(_bstr_t(L"\\TrafficMonitor"), &pTaskFolder);
    if (FAILED(hr))
    {
        if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && hr != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            ExitFunction();
        // Folder doesn't exist. Get the Root folder and create the TrafficMonitor subfolder.
        ITaskFolder* pRootFolder = NULL;
        hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
        ExitOnFailure(hr, "Cannot get Root Folder pointer: %x", hr);
        hr = pRootFolder->CreateFolder(_bstr_t(L"\\TrafficMonitor"), _variant_t(L""), &pTaskFolder);
        pRootFolder->Release();
        ExitOnFailure(hr, "Cannot create TrafficMonitor task folder: %x", hr);
    }

    // Create the task builder object to create the task.
    hr = pService->NewTask(0, &pTask);
    ExitOnFailure(hr, "Failed to create a task definition: %x", hr);

    // ------------------------------------------------------
    // Get the registration info for setting the identification.
    hr = pTask->get_RegistrationInfo(&pRegInfo);
    ExitOnFailure(hr, "Cannot get identification pointer: %x", hr);
    hr = pRegInfo->put_Author(_bstr_t(username_domain));
    ExitOnFailure(hr, "Cannot put identification info: %x", hr);

    // ------------------------------------------------------
    // Create the settings for the task
    hr = pTask->get_Settings(&pSettings);
    ExitOnFailure(hr, "Cannot get settings pointer: %x", hr);

    hr = pSettings->put_StartWhenAvailable(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_StartWhenAvailable setting info: %x", hr);
    hr = pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_StopIfGoingOnBatteries setting info: %x", hr);
    hr = pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); //Unlimited
    ExitOnFailure(hr, "Cannot put_ExecutionTimeLimit setting info: %x", hr);
    hr = pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_DisallowStartIfOnBatteries setting info: %x", hr);

    // ------------------------------------------------------
    // Get the trigger collection to insert the logon trigger.
    hr = pTask->get_Triggers(&pTriggerCollection);
    ExitOnFailure(hr, "Cannot get trigger collection: %x", hr);

    // Add the logon trigger to the task.
    {
        ITrigger* pTrigger = NULL;
        ILogonTrigger* pLogonTrigger = NULL;
        hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
        ExitOnFailure(hr, "Cannot create the trigger: %x", hr);

        hr = pTrigger->QueryInterface(
            IID_ILogonTrigger, (void**)&pLogonTrigger);
        pTrigger->Release();
        ExitOnFailure(hr, "QueryInterface call failed for ILogonTrigger: %x", hr);

        hr = pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));

        // Timing issues may make explorer not be started when the task runs.
        // Add a little delay to mitigate this.
        hr = pLogonTrigger->put_Delay(_bstr_t(L"PT03S"));

        // Define the user. The task will execute when the user logs on.
        // The specified user must be a user on this computer.
        hr = pLogonTrigger->put_UserId(_bstr_t(username_domain));
        pLogonTrigger->Release();
        ExitOnFailure(hr, "Cannot add user ID to logon trigger: %x", hr);
    }

    // ------------------------------------------------------
    // Add an Action to the task. This task will execute the path passed to this custom action.
    {
        IActionCollection* pActionCollection = NULL;
        IAction* pAction = NULL;
        IExecAction* pExecAction = NULL;

        // Get the task action collection pointer.
        hr = pTask->get_Actions(&pActionCollection);
        ExitOnFailure(hr, "Cannot get Action collection pointer: %x", hr);

        // Create the action, specifying that it is an executable action.
        hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
        pActionCollection->Release();
        ExitOnFailure(hr, "Cannot create the action: %x", hr);

        // QI for the executable task pointer.
        hr = pAction->QueryInterface(
            IID_IExecAction, (void**)&pExecAction);
        pAction->Release();
        ExitOnFailure(hr, "QueryInterface call failed for IExecAction: %x", hr);

        // Set the path of the executable to TrafficMonitor (passed as CustomActionData).
        hr = pExecAction->put_Path(_bstr_t(wszExecutablePath));
        pExecAction->Release();
        ExitOnFailure(hr, "Cannot set path of executable: %x", hr);
    }

    // ------------------------------------------------------
    // Create the principal for the task
    {
        IPrincipal* pPrincipal = NULL;
        hr = pTask->get_Principal(&pPrincipal);
        ExitOnFailure(hr, "Cannot get principal pointer: %x", hr);

        // Set up principal information:
        hr = pPrincipal->put_Id(_bstr_t(L"Principal1"));

        hr = pPrincipal->put_UserId(_bstr_t(username_domain));

        hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);

        if (runElevated)
        {
            hr = pPrincipal->put_RunLevel(_TASK_RUNLEVEL::TASK_RUNLEVEL_HIGHEST);
        }
        else
        {
            hr = pPrincipal->put_RunLevel(_TASK_RUNLEVEL::TASK_RUNLEVEL_LUA);
        }
        pPrincipal->Release();
        ExitOnFailure(hr, "Cannot put principal run level: %x", hr);
    }
    // ------------------------------------------------------
    //  Save the task in the TrafficMonitor folder.
    {
        // Task Scheduler adds the current principal's ACE. Other local users
        // must never be able to rewrite an elevated startup action.
        _variant_t taskSecurity = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
        hr = pTaskFolder->RegisterTaskDefinition(
            _bstr_t(wstrTaskName.c_str()),
            pTask,
            TASK_CREATE_OR_UPDATE | TASK_IGNORE_REGISTRATION_TRIGGERS,
            _variant_t(username_domain),
            _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            taskSecurity,
            &pRegisteredTask);
        ExitOnFailure(hr, "Error saving the Task : %x", hr);
    }

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();
    if (pTask)
        pTask->Release();
    if (pRegInfo)
        pRegInfo->Release();
    if (pSettings)
        pSettings->Release();
    if (pTriggerCollection)
        pTriggerCollection->Release();
    if (pRegisteredTask)
        pRegisteredTask->Release();

    // Positive scheduler warnings can mean that a trigger failed to register.
    return hr == S_OK;
}

bool delete_auto_start_task_for_this_user()
{
    HRESULT hr = S_OK;

    WCHAR username[USERNAME_LEN];
    std::wstring wstrTaskName;

    ITaskService* pService = NULL;
    ITaskFolder* pTaskFolder = NULL;

    // ------------------------------------------------------
    // Get the Username for the task.
    if (!GetEnvironmentVariable(L"USERNAME", username, USERNAME_LEN))
    {
        ExitWithLastError(hr, "Getting username failed: %x", hr);
    }

    // Task Name.
    wstrTaskName = L"Autorun for ";
    wstrTaskName += username;

    // ------------------------------------------------------
    // Create an instance of the Task Service.
    hr = CoCreateInstance(CLSID_TaskScheduler,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_ITaskService,
                          (void**)&pService);
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    // Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    // ------------------------------------------------------
    // Get the TrafficMonitor task folder.
    hr = pService->GetFolder(_bstr_t(L"\\TrafficMonitor"), &pTaskFolder);
    if (FAILED(hr))
    {
        // Only an absent folder counts as successful removal.
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) hr = S_OK;
        ExitFunction();
    }

    // ------------------------------------------------------
    // If the task exists, disable.
    {
        IRegisteredTask* pExistingRegisteredTask = NULL;
        hr = pTaskFolder->GetTask(_bstr_t(wstrTaskName.c_str()), &pExistingRegisteredTask);
        if (SUCCEEDED(hr))
        {
            // Task exists, try disabling it.
            hr = pTaskFolder->DeleteTask(_bstr_t(wstrTaskName.c_str()), 0);
            pExistingRegisteredTask->Release();
        }
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr = S_OK;
    }

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();

    return (SUCCEEDED(hr));
}

bool is_auto_start_task_active_for_this_user(std::wstring* path)
{
    HRESULT hr = S_OK;

    WCHAR username[USERNAME_LEN];
    std::wstring wstrTaskName;

    ITaskService* pService = NULL;
    ITaskFolder* pTaskFolder = NULL;

    bool command_path_match{};

    // ------------------------------------------------------
    // Get the Username for the task.
    if (!GetEnvironmentVariable(L"USERNAME", username, USERNAME_LEN))
    {
        ExitWithLastError(hr, "Getting username failed: %x", hr);
    }

    // Task Name.
    wstrTaskName = L"Autorun for ";
    wstrTaskName += username;

    // ------------------------------------------------------
    // Create an instance of the Task Service.
    hr = CoCreateInstance(CLSID_TaskScheduler,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_ITaskService,
                          (void**)&pService);
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    // Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    // ------------------------------------------------------
    // Get the TrafficMonitor task folder.
    hr = pService->GetFolder(_bstr_t(L"\\TrafficMonitor"), &pTaskFolder);
    ExitOnFailure(hr, "ITaskFolder doesn't exist: %x", hr);

    // ------------------------------------------------------
    // If the task exists, disable.
    {
        IRegisteredTask* pExistingRegisteredTask = NULL;
        hr = pTaskFolder->GetTask(_bstr_t(wstrTaskName.c_str()), &pExistingRegisteredTask);
        if (SUCCEEDED(hr))
        {
            // Task exists, get its value.
            VARIANT_BOOL is_enabled = VARIANT_FALSE;
            hr = pExistingRegisteredTask->get_Enabled(&is_enabled);
            // Use Task Scheduler's typed fields: XML escapes paths containing
            // '&', and Principal has an id attribute that the old text parser
            // cannot read. Only the sole elevated interactive action is active.
            CComPtr<ITaskDefinition> definition;
            CComPtr<IActionCollection> actions;
            CComPtr<IAction> action;
            CComPtr<IExecAction> executable;
            CComPtr<IPrincipal> principal;
            CComBSTR command;
            LONG actionCount = 0;
            TASK_RUNLEVEL_TYPE runLevel = TASK_RUNLEVEL_LUA;
            TASK_LOGON_TYPE logonType = TASK_LOGON_NONE;
            if (SUCCEEDED(hr)) hr = pExistingRegisteredTask->get_Definition(&definition);
            if (SUCCEEDED(hr)) hr = definition->get_Actions(&actions);
            if (SUCCEEDED(hr)) hr = actions->get_Count(&actionCount);
            if (SUCCEEDED(hr) && actionCount != 1) hr = E_FAIL;
            if (SUCCEEDED(hr)) hr = actions->get_Item(1, &action);
            if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&executable));
            if (SUCCEEDED(hr)) hr = executable->get_Path(&command);
            if (SUCCEEDED(hr)) hr = definition->get_Principal(&principal);
            if (SUCCEEDED(hr)) hr = principal->get_RunLevel(&runLevel);
            if (SUCCEEDED(hr)) hr = principal->get_LogonType(&logonType);
            if (FAILED(hr))
            {
                pExistingRegisteredTask->Release();
                ExitFunction();
            }
            std::wstring command_path = command ? static_cast<const wchar_t*>(command) : L"";
            if (path != nullptr)
                *path = command_path;
            WCHAR wszExecutablePath[MAX_PATH];
            const DWORD length = GetModuleFileName(NULL, wszExecutablePath, MAX_PATH);
            command_path_match = length > 0 && length < MAX_PATH
                && _wcsicmp(command_path.c_str(), wszExecutablePath) == 0
                && runLevel == TASK_RUNLEVEL_HIGHEST && logonType == TASK_LOGON_INTERACTIVE_TOKEN;

            pExistingRegisteredTask->Release();
            if (SUCCEEDED(hr))
            {
                // Got the value. Return it.
                hr = (is_enabled == VARIANT_TRUE) ? S_OK : E_FAIL; // Fake success or fail to return the value.
                ExitFunction();
            }
        }
    }

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();
    //delete[] buff;

    return (SUCCEEDED(hr) && command_path_match);
}
