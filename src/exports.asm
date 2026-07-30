OPTION CASEMAP:NONE

EXTERN real_DirectInput8Create:QWORD
EXTERN real_DllCanUnloadNow:QWORD
EXTERN real_DllGetClassObject:QWORD
EXTERN real_DllRegisterServer:QWORD
EXTERN real_DllUnregisterServer:QWORD
EXTERN real_GetdfDIJoystick:QWORD
EXTERN wrapped_DirectInput8Create:PROC

.code

FORWARD_EXPORT MACRO export_name:req
proxy_&export_name PROC
    jmp QWORD PTR [real_&export_name]
proxy_&export_name ENDP
ENDM

proxy_DirectInput8Create PROC
    jmp wrapped_DirectInput8Create
proxy_DirectInput8Create ENDP

FORWARD_EXPORT DllCanUnloadNow
FORWARD_EXPORT DllGetClassObject
FORWARD_EXPORT DllRegisterServer
FORWARD_EXPORT DllUnregisterServer
FORWARD_EXPORT GetdfDIJoystick

END
