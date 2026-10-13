import json,re,ntpath as P,collections
L=[l.rstrip('\n').split('\t') for l in open('inv.tsv',encoding='utf-8')]
gb=[l[1] for l in L if l[0]=='GB']
img=[l for l in L if l[0]=='IMG']
c1={P.splitext(P.basename(l[1]))[0].lower() for l in img if '\\IMGS\\CUSTOM\\' in l[1]}
st=lambda f:P.splitext(P.basename(f))[0]
grp=lambda f: 'debug' if '\\Debug Testing' in f else 'top' if '\\Top 50' in f else 'hack' if '\\Rom Hacks\\' in f else 'letter' if re.search(r'(^|\\)GBC?\\', f) else 'family'
lib={}
for r in ['Nintendo_-_Game_Boy_Color','Nintendo_-_Game_Boy']:
    for t in json.load(open(f'tree_{r}.json'))['tree']:
        p=t['path']
        if p.startswith('Named_Boxarts/') and p.endswith('.png'):
            n=p[14:-4]; lib.setdefault(n,(r,p))
def san(s): return re.sub(r'[&*/:`<>?\\|"]','_',s)
def core(s):
    s=re.sub(r'\[[^\]]*\]','',s); s=re.sub(r'\((Rev|v)[^)]*\)','',s,flags=re.I)
    t=re.sub(r'\([^)]*\)','',s)
    return re.sub(r'[^a-z0-9]','',t.lower())
def regions(s): return set(x.strip() for g in re.findall(r'\(([^)]*)\)',s) for x in g.split(','))
bycore=collections.defaultdict(list)
for n in lib: bycore[core(n)].append(n)
pref=['USA','World','Europe','Japan']
def best(s):
    if san(s) in lib: return san(s),'exact'
    c=bycore.get(core(s))
    if not c: return None,None
    rs=regions(s)
    c=sorted(c,key=lambda n:(-len(regions(n)&rs), min([pref.index(x) for x in regions(n) if x in pref] or [9]), len(n)))
    return c[0],'core'
out=[];cnt=collections.Counter()
for f in gb:
    g=grp(f); has=st(f).lower() in c1
    if g=='debug': cnt['debug-skip']+=1; continue
    m,k=best(st(f))
    out.append({'file':f,'grp':g,'custom':has,'lr':m,'lrrepo':lib[m][0] if m else None,'how':k})
    cnt[(has,k)]+=1
json.dump(out,open('match.json','w'),indent=0,ensure_ascii=False)
print(cnt)
for o in out:
    if not o['custom'] and not o['lr']: print('NOART',o['grp'],o['file'])
# titles fallback
tit={}
for r in ['Nintendo_-_Game_Boy_Color','Nintendo_-_Game_Boy']:
    for t in json.load(open(f'tree_{r}.json'))['tree']:
        p=t['path']
        if p.startswith('Named_Titles/') and p.endswith('.png'): tit.setdefault(p[13:-4],(r,p))
tcore=collections.defaultdict(list)
for n in tit: tcore[core(n)].append(n)
import os
srcdir='./work/out_art/src'
own={os.path.splitext(x)[0].lower():x for x in os.listdir(srcdir)}
c2=collections.Counter()
for o in out:
    if o['custom'] or o['lr']: continue
    s=st(o['file'])
    if s.lower() in own: o['own']=own[s.lower()]; c2['own']+=1; continue
    n=san(s) if san(s) in tit else (tcore.get(core(s)) or [None])[0]
    if n: o['title']=n; o['titlerepo']=tit[n][0]; c2['title']+=1
    else: c2['none']+=1; print('NONE',o['file'])
print(c2)
json.dump(out,open('match.json','w'),indent=0,ensure_ascii=False)
import difflib
cores=list(bycore)
def toks(s): return set(re.findall(r'[a-z0-9]+',re.sub(r'\([^)]*\)|\[[^\]]*\]','',s.lower())))-{'the','of','and','a'}
for o in out:
    if o['custom'] or o['lr'] or o.get('own') or o.get('title'): continue
    s=st(o['file']); c=core(s); T=toks(s)
    cand=[k for k in cores if (c in k or k in c) and min(len(c),len(k))>=5]
    if not cand:
        sc=sorted(((len(T&toks(bycore[k][0]))/max(1,len(T|toks(bycore[k][0]))),k) for k in cores),reverse=True)
        cand=[k for v,k in sc[:1] if v>=0.6]
    if cand:
        k=min(cand,key=lambda k:abs(len(k)-len(c))); rs=regions(s)
        n=sorted(bycore[k],key=lambda n:(-len(regions(n)&rs),len(n)))[0]
        o['lr']=n; o['lrrepo']=lib[n][0]; o['how']='fuzzy'; print('FUZZY',s,'=>',n)
    else: print('STILLNONE',o['file'])
json.dump(out,open('match.json','w'),indent=0,ensure_ascii=False)
