// Stand-in for Orbiter's SDK header, only for the organism trace: LifeSupport.cpp needs these few names, and the
// organism (Body) needs none of them. Lets the trace build and run without Orbiter.
#pragma once
typedef void* OBJHANDLE;
inline void oapiWriteLog(char*) {}
inline void oapiGetObjectName(OBJHANDLE, char* name, int n) { if (n > 0) name[0] = 0; }
class VESSEL
{
public:
	OBJHANDLE GetAtmRef() const { return nullptr; }
	double GetAtmTemperature() const { return 0; }
	double GetAtmPressure() const { return 0; }
};
