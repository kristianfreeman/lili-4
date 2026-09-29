#!/usr/bin/env python3
"""Flatten a Pd patch into readable object lists + connections per canvas.
GUI-only objects (cnv) and comments are dropped."""
import sys, re
def parse(path):
    txt = open(path).read()
    recs = [r.strip().replace('\n',' ') for r in re.split(r'(?<!\\);\s*\n', txt) if r.strip()]
    stack=[]; out=[]
    for r in recs:
        t = r.split()
        if t[0]=='#N' and t[1]=='canvas':
            stack.append({'name':t[6] if len(t)>7 else 'main','objs':[],'conns':[]}); continue
        cur=stack[-1]
        if t[0]=='#X':
            kind=t[1]
            if kind in('obj','msg','floatatom','symbolatom','text','listbox'):
                body=' '.join(t[4:]).rstrip(';') if kind!='floatatom' else 'floatatom'
                if t[4:5] and t[4] in ('inlet','inlet~','outlet','outlet~'): body+=f' @x={t[2]}'
                if kind=='msg': body='['+body+'(' 
                if kind=='text': body='// '+body
                cur['objs'].append(body)
            elif kind=='restore':
                sub=stack.pop(); out.append(sub)
                stack[-1]['objs'].append('pd '+' '.join(t[5:]).rstrip(';'))
            elif kind=='connect':
                cur['conns'].append(tuple(int(x) for x in t[2:6]))
            elif kind in ('coords','array','f'): pass
        elif t[0]=='#A': pass
    out.append(stack.pop())
    for c in out:
        print(f"=== canvas: {c['name']}")
        objs=c['objs']
        for i,o in enumerate(objs):
            if o.startswith('cnv '): continue
            ins=[f"{s}.{so}>{di}" for (s,so,d,di) in c['conns'] if d==i]
            outs=[f"{so}->{d}:{di}" for (s,so,d,di) in c['conns'] if s==i]
            print(f"  {i:3d} {o[:110]}" + (f"   <= {' '.join(ins)}" if ins else '') + (f"   => {' '.join(outs)}" if outs else ''))
for p in sys.argv[1:]:
    print(f"########## {p}"); parse(p)
