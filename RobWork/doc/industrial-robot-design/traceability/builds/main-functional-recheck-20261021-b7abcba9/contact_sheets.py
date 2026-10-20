"""生成截图索引，仅用于逐图审阅；不将像素非空自动判成视觉通过。"""
from pathlib import Path
from PIL import Image, ImageOps, ImageDraw
import json

out = Path(__file__).resolve().parent
index = []
for name in ('requirements-tour', 'modeling-tour', 'auto', 'layout'):
    shots = sorted((out/name).glob('*.png'))
    for start in range(0, len(shots), 9):
        canvas = Image.new('RGB', (1800, 1080), '#d0d0d0')
        draw = ImageDraw.Draw(canvas)
        page = shots[start:start+9]
        for i, path in enumerate(page):
            x, y = (i%3)*600, (i//3)*360
            im = Image.open(path).convert('RGB')
            canvas.paste(ImageOps.contain(im, (596, 328)), (x+2,y+28))
            draw.text((x+4,y+5), path.name, fill='black')
        target = out / 'contact-sheets' / f'{name}-{start//9+1}.jpg'
        target.parent.mkdir(exist_ok=True)
        canvas.save(target, quality=90)
        index.append(dict(sheet=str(target.relative_to(out)), originals=[str(p.relative_to(out)) for p in page]))
(out/'screenshot-index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')
print('\n'.join(x['sheet'] for x in index))
