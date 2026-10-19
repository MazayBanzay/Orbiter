import math
ve=0.98; dry=7.88; ts=0.817; cruise=math.atanh(0.834)
def budget(fuel):           # rapidity available
    M0=dry+4*(ts+fuel); Me=dry+4*ts
    return ve*math.log(M0/Me), M0
print("A. reserve vs fuel per trap (end mass dry+traps 11.15 kt)")
for f in (32,36,40,44,47.5,52):
    z,M0=budget(f); r=z-2*cruise
    print(f"  {f:5} kt: reserve rapidity {r:.3f} = {math.tanh(r):.3f} c, full {M0:.0f} kt")
print("B. lose one trap (fuel + container) at worst moment = right after acceleration; can we still brake to 0?")
for f in (32,36,40,44,47.5,52):
    M0=dry+4*(ts+f); M1=M0/math.exp(cruise/ve); fuel_left=M1-(dry+4*ts); per=fuel_left/4
    M2=M1-(per+ts); Me=dry+3*ts; z=ve*math.log(M2/Me)
    print(f"  {f:5} kt: after loss braking rapidity {z:.3f} need {cruise:.3f} -> {'OK' if z>=cruise else 'arrive at %.3f c'%math.tanh(cruise-z)}")
print("C. degraded jet (ve lower): 47.5 kt still brakes fully if ve >=")
z0,M0=budget(47.5); print(f"  {2*cruise/math.log(M0/(dry+4*ts)):.3f} c (nominal 0.98)")
print("D. course change at cruise 0.834c with reserve: angle you can turn the velocity (relativistic)")
g=1/math.sqrt(1-0.834**2)
for f in (36,40,47.5):
    r=budget(f)[0]-2*cruise
    # relative rapidity between two velocities of same speed separated by theta: cosh z = g^2 (1 - b^2 cos th)
    th=math.acos((1-math.cosh(r)/g**2)/0.834**2)
    print(f"  {f:5} kt: {math.degrees(th):.1f} deg (then still brakes nominally)")
print("E. heavier than planned: extra dry mass the reserve covers (still nominal 5/6 c both ways)")
for f in (36,40,47.5):
    M0=dry+4*(ts+f); Me=M0/math.exp(2*cruise/ve); print(f"  {f:5} kt: +{Me-(dry+4*ts):.1f} kt carried home")
