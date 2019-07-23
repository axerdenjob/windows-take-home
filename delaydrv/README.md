 Generally a bad idea to induce any kind of wait in callout function.
 It would be better to queue items and reinject after timeout in background thread.
 KeDelayExecutionThread would be better if possible (aside from being bad here to wait)
 but it causes BSOD because for good reason... windows does not like 
 delays in DPCs. This solution is a hack and is not very maintainable
 as the same "ATTEMPTED SWITCH FROM DPC" check could be added at 
 some point to StorPortStallExecution.

 Packet injection solution, implemented optimally...
 would need processing threads x logical cores
 and connect/packet queues per flow with locks
 to prevent out of band data injection.

/*LARGE_INTEGER intervalTime;

intervalTime.QuadPart = -1000000;

KeDelayExecutionThread(
    KernelMode,
    FALSE,
    &intervalTime
);*/

 StorPortStallExecution
 Documentation for this function notes..
 'The given value must be less than a full millisecond'
 This generally works, however may contemplate also adding a 
 time computation with KeQueryTickCount/KeQueryTimeIncrement.
 Note* CPU Usage on test VM is roughly 14% for this. Room for improvement by 
 moving to packet injection.
 