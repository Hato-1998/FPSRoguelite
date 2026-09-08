# ENE turnaround - drive Forge txt2img via REST API
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

def post(path, payload, timeout=900):
    req = urllib.request.Request(API + path,
        data=json.dumps(payload).encode('utf-8'),
        headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8'))

def get(path):
    with urllib.request.urlopen(API + path, timeout=60) as r:
        return json.loads(r.read().decode('utf-8'))

ckpt = sys.argv[1] if len(sys.argv) > 1 else None
seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1234567
tag  = sys.argv[3] if len(sys.argv) > 3 else 'v6'

if ckpt:
    cur = get('/sdapi/v1/options').get('sd_model_checkpoint', '')
    if ckpt.lower() not in cur.lower():
        print('switching checkpoint ->', ckpt, flush=True)
        post('/sdapi/v1/options', {'sd_model_checkpoint': ckpt}, timeout=600)

print('checkpoint :', get('/sdapi/v1/options').get('sd_model_checkpoint'), flush=True)
print('seed       :', seed, flush=True)
print('pos tokens ~', len(pos.split()), 'words', flush=True)

payload = {
    'prompt': pos,
    'negative_prompt': neg,
    'width': 832, 'height': 1216,
    'steps': 30, 'cfg_scale': 5.0,
    'sampler_name': 'Euler a',
    'scheduler': 'Automatic',
    'seed': seed,
    'batch_size': 1, 'n_iter': 1,
    'save_images': False,
}
r = post('/sdapi/v1/txt2img', payload)

os.makedirs(OUT, exist_ok=True)
stamp = datetime.datetime.now().strftime('%H%M%S')
short = re.sub(r'[^A-Za-z0-9]+', '', (ckpt or 'cur'))[:14]
paths = []
for k, b64 in enumerate(r['images']):
    p = os.path.join(OUT, '%s_%s_seed%d_%s.png' % (tag, short, seed, stamp))
    if k: p = p.replace('.png', '_%d.png' % k)
    with open(p, 'wb') as f:
        f.write(base64.b64decode(b64.split(',', 1)[-1]))
    paths.append(p)
    print('saved      :', p, flush=True)

info = json.loads(r.get('info', '{}'))
print('actual seed:', info.get('seed'), flush=True)
print('DONE', flush=True)
