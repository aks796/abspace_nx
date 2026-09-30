#!/usr/bin/env python3
"""luals.py file.lua [funcname-filter] -- list Lua 5.1 bytecode (Rovio: 4-byte float numbers)."""
import sys, struct
OP=["MOVE","LOADK","LOADBOOL","LOADNIL","GETUPVAL","GETGLOBAL","GETTABLE","SETGLOBAL","SETUPVAL",
"SETTABLE","NEWTABLE","SELF","ADD","SUB","MUL","DIV","MOD","POW","UNM","NOT","LEN","CONCAT","JMP",
"EQ","LT","LE","TEST","TESTSET","CALL","TAILCALL","RETURN","FORLOOP","FORPREP","TFORLOOP","SETLIST",
"CLOSE","CLOSURE","VARARG"]
ABC,ABx,AsBx=0,1,2
MODE=[ABC,ABx,ABC,ABC,ABC,ABx,ABC,ABx,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,AsBx,
ABC,ABC,ABC,ABC,ABC,ABC,ABC,ABC,AsBx,AsBx,ABC,ABC,ABC,ABx,ABC]
class R:
    def __init__(s,b): s.b=b; s.p=0
    def u8(s): v=s.b[s.p]; s.p+=1; return v
    def i32(s): v=struct.unpack_from('<i',s.b,s.p)[0]; s.p+=4; return v
    def u32(s): v=struct.unpack_from('<I',s.b,s.p)[0]; s.p+=4; return v
    def f32(s): v=struct.unpack_from('<f',s.b,s.p)[0]; s.p+=4; return v
    def str(s):
        n=s.u32()
        if n==0: return None
        v=s.b[s.p:s.p+n-1].decode('latin1'); s.p+=n; return v
def func(r, name, depth, out):
    src=r.str(); line=r.i32(); lastline=r.i32(); nup=r.u8(); npar=r.u8(); va=r.u8(); ms=r.u8()
    n=r.i32(); code=[r.u32() for _ in range(n)]
    n=r.i32(); K=[]
    for _ in range(n):
        t=r.u8()
        if t==0: K.append(None)
        elif t==1: K.append(bool(r.u8()))
        elif t==3: K.append(r.f32())
        elif t==4: K.append(r.str())
        else: raise Exception('const type %d'%t)
    n=r.i32(); protos=[]
    fns=[]
    for i in range(n):
        fns.append(r.p)
        protos.append(None)
    # need to parse children sequentially
    r.p=fns[0] if fns else r.p
    kids=[]
    for i in range(n):
        kids.append(func(r, '%s.%d'%(name,i), depth+1, out))
    # debug
    n=r.i32(); r.p+=4*n
    n=r.i32()
    for _ in range(n): r.str(); r.i32(); r.i32()
    n=r.i32(); ups=[r.str() for _ in range(n)]
    out.append((name,npar,va,nup,code,K,kids))
    return name
def rk(K,x):
    if x&0x100:
        v=K[x&0xff]; return repr(v) if not isinstance(v,float) else ('%g'%v)
    return 'r%d'%x
def listing(name,npar,va,nup,code,K):
    print('== function %s (params %d%s, upvals %d, %d insns)'%(name,npar,'+...' if va&2 else '',nup,len(code)))
    for pc,i in enumerate(code):
        o=i&63; a=(i>>6)&255; c=(i>>14)&511; b=(i>>23)&511; bx=i>>14; sbx=bx-131071
        m=MODE[o] if o<len(MODE) else ABC; nm=OP[o] if o<len(OP) else '?%d'%o
        if nm in('GETGLOBAL','SETGLOBAL'): s='%s r%d %s'%(nm,a,K[bx])
        elif nm=='LOADK': s='LOADK r%d %s'%(a,rk(K,bx|0x100) if bx<256 else repr(K[bx]))
        elif nm in('GETTABLE','SELF'): s='%s r%d r%d[%s]'%(nm,a,b,rk(K,c))
        elif nm=='SETTABLE': s='SETTABLE r%d[%s] = %s'%(a,rk(K,b),rk(K,c))
        elif nm in('ADD','SUB','MUL','DIV','MOD','POW','EQ','LT','LE'): s='%s %d %s %s'%(nm,a,rk(K,b),rk(K,c))
        elif m==AsBx: s='%s r%d ->%d'%(nm,a,pc+1+sbx)
        elif m==ABx: s='%s r%d %d'%(nm,a,bx)
        else: s='%s %d %d %d'%(nm,a,b,c)
        print('  %4d  %s'%(pc,s))
if __name__=='__main__':
    b=open(sys.argv[1],'rb').read(); r=R(b); r.p=12
    out=[]; func(r,'main',0,out)
    flt=sys.argv[2] if len(sys.argv)>2 else None
    # name functions by the SETGLOBAL/SETTABLE after CLOSURE in parent
    names={}
    for (name,npar,va,nup,code,K,kids) in out:
        for pc,i in enumerate(code):
            if i&63==36:
                bx=i>>14; a=(i>>6)&255
                # find next SETGLOBAL/SETTABLE using reg a
                for j in range(pc+1,min(pc+1+nup+8,len(code))):
                    ins=code[j]; o=ins&63
                    if o==7 and ((ins>>6)&255)==a: names[kids[bx]]=K[ins>>14]; break
                    if o==9 and ((ins>>14)&511)==a:
                        bb=(ins>>23)&511
                        if bb&0x100: names[kids[bx]]='.'+str(K[bb&0xff])
                        break
    for (name,npar,va,nup,code,K,kids) in out:
        nm=names.get(name,'')
        if flt and flt not in nm: continue
        listing(name+(' '+nm if nm else ''),npar,va,nup,code,K)
