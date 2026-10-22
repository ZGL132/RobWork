"""WP-13-T30：重算本机观测视口的工程蓝像素，仅辅助逐张目视验收。"""
from pathlib import Path
from PIL import Image
import json
root=Path(__file__).resolve().parent
rows=[]
for attempt,folder,prefix in [('batch4-visual-final','red','visual-before-'),('batch4-overlay-final','screenshots','')]:
    for target in ['j1','j2','link1']:
        name=f'mtour-t30-selected-{target}.png'
        image=Image.open(root/folder/(prefix+name)).convert('RGB')
        # 裁剪来自此次截图所见中央三维视口；换DPI/布局必须重新观测边界。
        crop=(781,78,1523,image.height-28)
        count=sum(pixel==(30,90,168) for pixel in image.crop(crop).getdata())
        rows.append(dict(attempt=attempt,image=name,viewCrop=crop,primaryRgb=[30,90,168],primaryPixels=count))
print(json.dumps(rows,ensure_ascii=False,indent=2))
