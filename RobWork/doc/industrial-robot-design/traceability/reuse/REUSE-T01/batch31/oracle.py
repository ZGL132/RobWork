from fractions import Fraction
import sys,json
m=Fraction.from_float(sys.float_info.max)
diff=abs(m-(-m));limit=Fraction(3,2)*abs(-m)
assert diff>limit
print(json.dumps(dict(valueHex=sys.float_info.max.hex(),referenceHex=(-sys.float_info.max).hex(),relative='3/2',absolute=0,differenceOverMaximum=str(diff/m),limitOverMaximum=str(limit/m),expected=False),indent=2))
