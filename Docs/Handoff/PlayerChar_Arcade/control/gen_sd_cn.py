# ENE turnaround - txt2img with ControlNet (depth blockout) via Forge REST API.
# Prompt is READ FROM THE DOC so the doc stays the single source of truth.
#
#   python gen_sd_cn.py <depth.png> <tag> <weight> <seed> <width> <view> [ref.png|-] [ref_w]
#
import io, os, re, sys, json, base64, urllib.request, datetime

DOC = r'E:\Git_Project\FPSRoguelite\Docs\Handoff\PlayerChar_Arcade\ImagePrompt_v5_SD.txt'
OUT = r'E:\Git_Project\FPSRoguelite\Docs\Handoff\PlayerChar_Arcade\sd_out'
API = 'http://127.0.0.1:7860'

DOCTEXT = io.open(DOC, encoding='utf-8').read()

def flat(s):
    return ' '.join(l.strip() for l in s.strip().splitlines() if l.strip())

def block(tag, nxt):
    i = DOCTEXT.index(tag); j = DOCTEXT.index(nxt, i); b = DOCTEXT[i:j]
    return flat(b[b.index('```') + 3: b.rindex('```')])

def fence_after(marker, start=0):
    i = DOCTEXT.index(marker, start)
    a = DOCTEXT.index('```', i) + 3
    return flat(DOCTEXT[a: DOCTEXT.index('```', a)])

pos = block('### 3-1.', '### 3-2.')
neg = block('### 3-2.', '### 3-3.')

# per-view extra negatives (doc 3-3-2). Swapping the view clause alone does not
# beat the model's front-facing prior - the front has to be suppressed too.
VIEW_NEG_MARK = {'side': u'**\uce21\uba74(`side`)**', 'back': u'**\ud6c4\uba74(`back`)**'}

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
ref_path  = sys.argv[7] if len(sys.argv) > 7 else '-'
ref_w     = float(sys.argv[8]) if len(sys.argv) > 8 else 0.45

# swap ONLY the view clause; every other token stays byte-identical across views
# (that is what keeps the three sheets the same character). Side/back get 1.5:
# they run against the model's prior, so front's 1.3 is not enough (doc 3-3-3).
if view != 'front':
    m = re.search(r'\(front view, facing viewer:[0-9.]+\)', pos)
    if m:
        pos = pos[:m.start()] + '(%s:1.5)' % VIEWS[view] + pos[m.end():]
    else:
        assert VIEWS['front'] in pos, 'front view clause not found in prompt'
        pos = pos.replace(VIEWS['front'], '(%s:1.5)' % VIEWS[view])
    neg = neg + ', ' + fence_after(VIEW_NEG_MARK[view], DOCTEXT.index('### 3-3-2.'))

print('view     :', view, '->', VIEWS[view])
print('view neg :', 'appended' if view != 'front' else '(none)')

with open(pose_path, 'rb') as f:
    pose_b64 = base64.b64encode(f.read()).decode('ascii')

models = get('/controlnet/model_list')['model_list']
union = [m for m in models if 'union' in m.lower()]
if not union:
    print('ERROR: union controlnet model not found:', models); sys.exit(1)
model = union[0]
print('cn model :', model)
print('weight   :', weight)

units = [{
    'enabled': True,
    'image': pose_b64,
    'module': 'None',          # blockout is already a depth map - do NOT preprocess
    'model': model,
    'weight': weight,
    'resize_mode': 'Just Resize',
    'control_mode': 'Balanced',
    'pixel_perfect': True,
    'guidance_start': 0.0,
    'guidance_end': 0.85,      # release near the end so style/detail can finish
}]

# reference_only - doc 2 step 2, finally implemented (doc 3-3-4). Without it the
# side/back sheets came back as a different outfit entirely (v8b). It cuts both
# ways: feeding a FRONT reference also drags the composition back toward front,
# so it releases early (0.55) and its weight is the first dial to drop.
if ref_path not in ('-', '', 'none'):
    mods = get('/controlnet/module_list')['module_list']
    pick = (next((m for m in mods if m.lower() == 'reference_only'), None)
            or next((m for m in mods if 'reference' in m.lower()), None))
    if not pick:
        print('ERROR: reference_only module not available:', mods); sys.exit(1)
    with open(ref_path, 'rb') as f:
        ref_b64 = base64.b64encode(f.read()).decode('ascii')
    units.append({
        'enabled': True,
        'image': ref_b64,
        'module': pick,
        'model': 'None',
        'weight': ref_w,
        'resize_mode': 'Just Resize',
        'control_mode': 'Balanced',
        'pixel_perfect': True,
        'guidance_start': 0.0,
        'guidance_end': 0.55,
        'threshold_a': 0.5,    # Forge: reference "Style Fidelity" (Balanced only)
    })
    print('ref unit : %s  w=%s  %s' % (pick, ref_w, os.path.basename(ref_path)))

payload = {
    'prompt': pos, 'negative_prompt': neg,
    'width': width, 'height': 1216,
    'steps': 30, 'cfg_scale': 5.0,
    'sampler_name': 'Euler a', 'scheduler': 'Automatic',
    'seed': seed, 'batch_size': 1, 'n_iter': 1, 'save_images': False,
    'alwayson_scripts': {'controlnet': {'args': units}},
}
r = post('/sdapi/v1/txt2img', payload)

os.makedirs(OUT, exist_ok=True)
stamp = datetime.datetime.now().strftime('%H%M%S')
p = os.path.join(OUT, '%s_w%s_seed%d_%s.png' % (tag, str(weight).replace('.', ''), seed, stamp))
with open(p, 'wb') as f:
    f.write(base64.b64decode(r['images'][0].split(',', 1)[-1]))
print('saved    :', p)
print('DONE')
