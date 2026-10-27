from fractions import Fraction
from sys import float_info
import json
maximum=Fraction.from_float(float_info.max)
value,reference=maximum,-maximum
relative,absolute=Fraction(3,2),Fraction(0)
difference=abs(value-reference)
limit=relative*abs(reference)+absolute
assert difference/maximum == 2
assert limit/maximum == Fraction(3,2)
assert not difference <= limit
print(json.dumps({'oracle':'Python Fraction exact rational; independently recomputed in acceptance','differenceOverMaximum':str(difference/maximum),'limitOverMaximum':str(limit/maximum),'expected':bool(difference<=limit),'passed':True}))
