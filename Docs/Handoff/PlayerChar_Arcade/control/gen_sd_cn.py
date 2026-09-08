# ENE turnaround - txt2img with ControlNet (OpenPose skeleton) via Forge REST API.
# Prompt is READ FROM THE DOC so the doc stays the single source of truth.
import io, os, re, sys, json, base64, urllib.request, datetime

DOC = r'E:\Git_Project\FPSRoguelite\Docs\Handoff\PlayerChar_Arcade\ImagePrompt_v5_SD.txt'
OUT = r'E:\Git_Project\FPSRoguelite\Docs\Handoff\PlayerChar_Arcade\sd_out'
API = 'http://127.0.0.1:7860'

def block(tag, nxt):
    s = io.open(DOC, encoding='utf-8').read()
    i = s.index(tag); j = s.index(nxt, i); b = s[i:j]
    return b[b.index('```') + 3: b.rindex('```')].strip()

pos = ' '.join(l.strip() for l in block('### 3-1.', '### 3-2.').splitlines() if l.strip())
neg = ' '.join(l.strip() for l in block('### 3-2.', '### 3-3.').splitlines() if l.strip())

def post(path, payload, timeout=1200):
    req = urllib.request.Request(API + path, data=json.dumps(payload).encode('utf-8'),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8'))

def get(path):
    with urllib.request.urlopen(API + path, timeout=60) as r:
        return json.loads(r.read().decode('utf-8'))

VIEWS = {
    'front': 'front view, facing viewer',
    'side' : 'from side, profile, facing right',
    'back' : 'from behind, back view, facing away from viewer',
}
pose_path = sys.argv[1]
tag       = sys.argv[2] if len(sys.argv) > 2 else 'cn'
weight    = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
seed      = int(sys.argv[4]) if len(sys.argv) > 4 else 1234567
width     = int(sys.argv[5]) if len(sys.argv) > 5 else 832
view      = (sys.argv[6] if len(sys.argv) > 6 else 'front').lower()

# swap ONLY the view clause; every other token stays byte-identical across views
# (that is what keeps the three sheets the same character)
if view != 'front':
    assert VIEWS['front'] in pos, 'front view clause not found in prompt'
    pos = pos.replace(VIEWS['front'], VIEWS[view])
print('view     :', view)

with open(pose_path, 'rb') as f:
    pose_b64 = base64.b64encode(f.read()).decode('ascii')

models = get('/controlnet/model_list')['model_list']
union = [m for m in models if 'union' in m.lower()]
if not union:
    print('ERROR: union controlnet model not found:', models); sys.exit(1)
model = union[0]
print('cn model :', model)
print('weight   :', weight)

cn_unit = {
    'enabled': True,
    'image': pose_b64,
    'module': 'None',          # skeleton is already a pose map - do NOT preprocess
    'model': model,
    'weight': weight,
    'resize_mode': 'Just Resize',
    'control_mode': 'Balanced',
    'pixel_perfect': True,
    'guidance_start': 0.0,
    'guidance_end': 0.85,      # release near the end so style/detail can finish
}

payload = {
    'prompt': pos, 'negative_prompt': neg,
    'width': width, 'height': 1216,
    'steps': 30, 'cfg_scale': 5.0,
    'sampler_name': 'Euler a', 'scheduler': 'Automatic',
    'seed': seed, 'batch_size': 1, 'n_iter': 1, 'save_images': False,
    'alwayson_scripts': {'controlnet': {'args': [cn_unit]}},
}
r = post('/sdapi/v1/txt2img', payload)

os.makedirs(OUT, exist_ok=True)
stamp = datetime.datetime.now().strftime('%H%M%S')
p = os.path.join(OUT, '%s_w%s_seed%d_%s.png' % (tag, str(weight).replace('.', ''), seed, stamp))
with open(p, 'wb') as f:
    f.write(base64.b64decode(r['images'][0].split(',', 1)[-1]))
print('saved    :', p)
print('DONE')
