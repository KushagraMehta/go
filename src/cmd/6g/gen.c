// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#undef	EXTERN
#define	EXTERN
#include "gg.h"

enum
{
	// random unused opcode
	AJMPX	= AADDPD,
};

static	Node*	curfn;
static	Node*	newproc;

void
compile(Node *fn)
{
	Plist *pl;
	Node nod1;
	Prog *ptxt;
	long lno;

if(newproc == N) {
	newproc = nod(ONAME, N, N);
	memset(newproc, 0, sizeof(*newproc));
	newproc->op = ONAME;
	newproc->sym = pkglookup("newproc", "sys");
	newproc->class = PEXTERN;
	newproc->addable = 1;
	newproc->ullman = 0;
}

	if(fn->nbody == N)
		return;
	lno = setlineno(fn);

	curfn = fn;
	dowidth(curfn->type);

	walk(curfn);
	if(nerrors != 0)
		goto ret;

	allocparams();

	continpc = P;
	breakpc = P;

	pl = newplist();
	pl->name = curfn->nname;
	pl->locals = autodcl;

	nodconst(&nod1, types[TINT32], 0);
	ptxt = gins(ATEXT, curfn->nname, &nod1);

//	inarggen();

	ginit();
	gen(curfn->nbody);
	gclean();

//	if(curfn->type->outtuple != 0)
//		gins(AGOK, N, N);

	pc->as = ARET;	// overwrite AEND
	pc->lineno = lineno;

	// fill in argument size
	ptxt->to.offset = rnd(curfn->type->argwid, maxround);

	// fill in final stack size
	ptxt->to.offset <<= 32;
	ptxt->to.offset |= rnd(stksize+maxarg, maxround);

	if(debug['f'])
		frame(0);

ret:
	lineno = lno;
}

void
allocparams(void)
{
	Dcl *d;
	Iter list;
	Type *t;
	Node *n;
	ulong w;

	/*
	 * allocate (set xoffset) the stack
	 * slots for this, inargs, outargs
	 * these are allocated positavely
	 * from 0 up.
	 * note that this uses the 'width'
	 * field, which, in the OFIELD of the
	 * parameters, is the offset in the
	 * parameter list.
	 */
	d = curfn->type->param->forw;
	t = funcfirst(&list, curfn->type);
	while(t != T) {
		if(d == D)
			fatal("allocparams: this & in nil");

		if(d->op != ONAME) {
			d = d->forw;
			continue;
		}

		n = d->dnode;
		if(n->class != PPARAM)
			fatal("allocparams: this & in class %N %d", n, n->class);

//print("assign %S %ld\n", n->sym, t->width);
		n->xoffset = t->width;
		d = d->forw;
		t = funcnext(&list);
	}

	t = structfirst(&list, getoutarg(curfn->type));
	while(t != T) {
		if(t->nname != N && t->nname->sym->name[0] != '_') {
			if(d == D)
				fatal("allocparams: out nil");

			if(d->op != ONAME) {
				d = d->forw;
				continue;
			}

			n = d->dnode;
			if(n->class != PPARAM)
				fatal("allocparams: out class %N %d", n, n->class);

			n->xoffset = t->width;
			d = d->forw;
		}
		t = structnext(&list);
	}

	/*
	 * allocate (set xoffset) the stack
	 * slots for all automatics.
	 * allocated starting at -w down.
	 */
	for(d=autodcl; d!=D; d=d->forw) {
		if(d->op != ONAME)
			continue;

		n = d->dnode;
		if(n->class != PAUTO)
			continue;

		dowidth(n->type);
		w = n->type->width;
		stksize += w;
		stksize = rnd(stksize, w);

		n->xoffset = -stksize;
	}
}

/*
 * compile statements
 */
void
gen(Node *n)
{
	long lno;
	Prog *scontin, *sbreak;
	Prog *p1, *p2, *p3;
	Sym *s;

	lno = setlineno(n);

loop:
	if(n == N)
		goto ret;
	setlineno(n);

	switch(n->op) {
	default:
		fatal("gen: unknown op %N", n);
		break;

	case OLIST:
		gen(n->left);
		n = n->right;
		goto loop;

	case OPANIC:
		genpanic();
		break;

	case OCASE:
	case OFALL:
	case OXCASE:
	case OXFALL:
	case OEMPTY:
		break;

	case OLABEL:
		// before declaration, s->label points at
		// a link list of PXGOTO instructions.
		// after declaration, s->label points
		// at a AJMP to .+1

		s = n->left->sym;
		p1 = (Prog*)s->label;

		if(p1 != P) {
			if(p1->as == AJMP) {
				yyerror("label redeclared: %S", s);
				break;
			}
			while(p1 != P) {
				if(p1->as != AJMPX)
					fatal("bad label pointer: %S", s);
				p1->as = AJMP;
				p2 = p1->to.branch;
				patch(p1, pc);
				p1 = p2;
			}
		}

		s->label = pc;
		p1 = gbranch(AJMP, T);
		patch(p1, pc);
		break;

	case OGOTO:
		s = n->left->sym;
		p1 = (Prog*)s->label;
		if(p1 != P && p1->as == AJMP) {
			// already declared
			p2 = gbranch(AJMP, T);
			patch(p2, p1->to.branch);
			break;
		}

		// link thru to.branch
		p2 = gbranch(AJMPX, T);
		p2->to.branch = p1;
		s->label = p2;
		break;

	case OBREAK:
		if(breakpc == P) {
			yyerror("gen: break is not in a loop");
			break;
		}
		patch(gbranch(AJMP, T), breakpc);
		break;

	case OCONTINUE:
		if(continpc == P) {
			yyerror("gen: continue is not in a loop");
			break;
		}
		patch(gbranch(AJMP, T), continpc);
		break;

	case OFOR:
		gen(n->ninit);				// 		init
		p1 = gbranch(AJMP, T);			// 		goto test
		sbreak = breakpc;
		breakpc = gbranch(AJMP, T);		// break:	goto done
		scontin = continpc;
		continpc = pc;
		gen(n->nincr);				// contin:	incr
		patch(p1, pc);				// test:
		bgen(n->ntest, 0, breakpc);		//		if(!test) goto break
		gen(n->nbody);				//		body
		patch(gbranch(AJMP, T), continpc);	//		goto contin
		patch(breakpc, pc);			// done:
		continpc = scontin;
		breakpc = sbreak;
		break;

	case OIF:
		gen(n->ninit);				//		init
		p1 = gbranch(AJMP, T);			//		goto test
		p2 = gbranch(AJMP, T);			// p2:		goto else
		patch(p1, pc);				// test:
		bgen(n->ntest, 0, p2);			// 		if(!test) goto p2
		gen(n->nbody);				//		then
		p3 = gbranch(AJMP, T);			//		goto done
		patch(p2, pc);				// else:
		gen(n->nelse);				//		else
		patch(p3, pc);				// done:
		break;

	case OSWITCH:
		gen(n->ninit);				// 		init
		p1 = gbranch(AJMP, T);			// 		goto test
		sbreak = breakpc;
		breakpc = gbranch(AJMP, T);		// break:	goto done
		patch(p1, pc);				// test:
		swgen(n);				//		switch(test) body
		patch(breakpc, pc);			// done:
		breakpc = sbreak;
		break;

	case OASOP:
		cgen_asop(n);
		break;

	case OAS:
		cgen_as(n->left, n->right, n->op);
		break;

	case OCALLMETH:
		cgen_callmeth(n, 0);
		break;

	case OCALLINTER:
		cgen_callinter(n, N, 0);
		break;

	case OCALL:
		cgen_call(n, 0);
		break;

	case OPROC:
		cgen_proc(n);
		break;

	case ORETURN:
		cgen_ret(n);
		break;
	}

ret:
	lineno = lno;
}

void
agen_inter(Node *n, Node *res)
{
	Node nodo, nodr, nodt;
	Sym *s;
	char *e;
	long o,lno;

	lno = setlineno(n);

	// stack offset
	memset(&nodo, 0, sizeof(nodo));
	nodo.op = OINDREG;
	nodo.val.vval = D_SP;
	nodo.addable = 1;
	nodo.type = types[tptr];

	// pointer register
	regalloc(&nodr, types[tptr], res);

	switch(n->op) {
	default:
		fatal("agen_inter %O\n", n->op);

	case OS2I:
		// ifaces2i(*sigi, *sigs, i.map, i.s)
		// i.s is input
		// (i.map, i.s) is output

		cgen(n->left, &nodr);
		nodo.xoffset = 3*widthptr;
		cgen_as(&nodo, &nodr, 0);

		nodtypesig(&nodt, n->type);
		agen(&nodt, &nodr);
		nodo.xoffset = 0*widthptr;
		cgen_as(&nodo, &nodr, 0);

		nodtypesig(&nodt, n->left->type);
		agen(&nodt, &nodr);
		nodo.xoffset = 1*widthptr;
		cgen_as(&nodo, &nodr, 0);

		e = "ifaces2i";
		if(maxarg < 4*widthptr)
			maxarg = 4*widthptr;
		o = 2*widthptr;
		break;

	case OI2I:
		// ifacei2i(*sigi, i.map, i.s)
		// (i.map, i.s) is input
		// (i.map, i.s) is output

		nodo.xoffset = 1*widthptr;
		if(!n->left->addable) {
			agen(n->left, &nodr);
			gmove(&nodr, &nodo);
			fatal("agen_inter i2i");
		} else {
			cgen(n->left, &nodo);
		}

		nodtypesig(&nodt, n->type);
		agen(&nodt, &nodr);
		nodo.xoffset = 0*widthptr;
		cgen_as(&nodo, &nodr, 0);

		e = "ifacei2i";
		if(maxarg < 3*widthptr)
			maxarg = 3*widthptr;
		o = 1*widthptr;
		break;

	case OI2S:
		// ifacei2s(*sigs, i.map, i.s)
		// (i.map, i.s) is input
		// i.s is output

		nodo.xoffset = 1*widthptr;
		if(!n->left->addable) {
			agen(n->left, &nodr);
			gmove(&nodr, &nodo);
			dump("xxx", n->left);
			fatal("agen_inter i2s");
		} else {
			cgen(n->left, &nodo);
		}

		nodtypesig(&nodt, n->type);
		agen(&nodt, &nodr);
		nodo.xoffset = 0*widthptr;
		cgen_as(&nodo, &nodr, 0);

		e = "ifacei2s";
		if(maxarg < 3*widthptr)
			maxarg = 3*widthptr;
		o = 2*widthptr;
		break;
	}

	s = pkglookup(e, "sys");
	if(s->oname == N) {
		s->oname = newname(s);
		s->oname->class = PEXTERN;
	}
	gins(ACALL, N, s->oname);

	nodo.xoffset = o;
	gins(ALEAQ, &nodo, res);

	regfree(&nodr);
	lineno = lno;
}

void
swgen(Node *n)
{
	Node *c1, *c2;
	Node n1, tmp;
	Case *s0, *se, *s;
	Prog *p1, *dflt;
	long lno;
	int any;
	Iter save1, save2;

// botch - put most of this code in
// walk. gen binary search for
// sequence of constant cases

	lno = setlineno(n);

	p1 = gbranch(AJMP, T);
	s0 = C;
	se = C;

	// walk thru the body placing breaks
	// and labels into the case statements

	any = 0;
	dflt = P;
	c1 = listfirst(&save1, &n->nbody);
	while(c1 != N) {
		setlineno(c1);
		if(c1->op != OCASE) {
			if(s0 == C && dflt == P)
				yyerror("unreachable statements in a switch");
			gen(c1);

			any = 1;
			if(c1->op == OFALL)
				any = 0;
			c1 = listnext(&save1);
			continue;
		}

		// put in the break between cases
		if(any) {
			patch(gbranch(AJMP, T), breakpc);
			any = 0;
		}

		// over case expressions
		c2 = listfirst(&save2, &c1->left);
		if(c2 == N)
			dflt = pc;

		while(c2 != N) {
			s = mal(sizeof(*s));
			if(s0 == C)
				s0 = s;
			else
				se->slink = s;
			se = s;

			s->scase = c2;		// case expression
			s->sprog = pc;		// where to go

			c2 = listnext(&save2);
		}

		c1 = listnext(&save1);
	}

	lineno = lno;

	if(any)
		patch(gbranch(AJMP, T), breakpc);

	patch(p1, pc);

	tempname(&tmp, n->ntest->type);
	cgen(n->ntest, &tmp);

	for(s=s0; s!=C; s=s->slink) {
		setlineno(s->scase);
		memset(&n1, 0, sizeof(n1));
		n1.op = OEQ;
		n1.left = &tmp;
		n1.right = s->scase;
		walktype(&n1, Erv);
		bgen(&n1, 1, s->sprog);
	}
	if(dflt != P) {
		patch(gbranch(AJMP, T), dflt);
		goto ret;
	}
	patch(gbranch(AJMP, T), breakpc);

ret:
	lineno = lno;
}

void
inarggen(void)
{
	fatal("inarggen");
}

void
genpanic(void)
{
	Node n1, n2;
	Prog *p;

	nodconst(&n1, types[TINT64], 0xf0);
	nodreg(&n2, types[TINT64], D_AX);
	gins(AMOVL, &n1, &n2);
	p = pc;
	gins(AMOVQ, &n2, N);
	p->to.type = D_INDIR+D_AX;
}

int
argsize(Type *t)
{
	Iter save;
	Type *fp;
	int w, x;

	w = 0;

	fp = structfirst(&save, getoutarg(t));
	while(fp != T) {
		x = fp->width + fp->type->width;
		if(x > w)
			w = x;
		fp = structnext(&save);
	}

	fp = funcfirst(&save, t);
	while(fp != T) {
		x = fp->width + fp->type->width;
		if(x > w)
			w = x;
		fp = structnext(&save);
	}

	w = (w+7) & ~7;
	return w;
}

void
ginscall(Node *f, int proc)
{
	Node reg, con;

	if(proc) {
		nodreg(&reg, types[TINT64], D_AX);
		if(f->op != OREGISTER) {
			gins(ALEAQ, f, &reg);
			gins(APUSHQ, &reg, N);
		} else
			gins(APUSHQ, f, N);
		nodconst(&con, types[TINT32], argsize(f->type));
		gins(APUSHQ, &con, N);
		gins(ACALL, N, newproc);
		gins(APOPQ, N, &reg);
		gins(APOPQ, N, &reg);
		return;
	}
	gins(ACALL, N, f);
}

void
cgen_callinter(Node *n, Node *res, int proc)
{
	Node *i, *f;
	Node tmpi, nodo, nodr, nodsp;

	i = n->left;
	if(i->op != ODOTINTER)
		fatal("cgen_callinter: not ODOTINTER %O", i->op);

	f = i->right;		// field
	if(f->op != ONAME)
		fatal("cgen_callinter: not ONAME %O", f->op);

	i = i->left;		// interface

	if(!i->addable) {
		tempname(&tmpi, i->type);
		cgen(i, &tmpi);
		i = &tmpi;
	}

	gen(n->right);		// args

	regalloc(&nodr, types[tptr], res);
	regalloc(&nodo, types[tptr], &nodr);
	nodo.op = OINDREG;

	agen(i, &nodr);		// REG = &inter

	nodindreg(&nodsp, types[tptr], D_SP);
	nodo.xoffset += widthptr;
	cgen(&nodo, &nodsp);	// 0(SP) = 8(REG) -- i.s

	nodo.xoffset -= widthptr;
	cgen(&nodo, &nodr);	// REG = 0(REG) -- i.m

	nodo.xoffset = n->left->xoffset + 4*widthptr;
	cgen(&nodo, &nodr);	// REG = 32+offset(REG) -- i.m->fun[f]

	// BOTCH nodr.type = fntype;
	ginscall(&nodr, proc);

	regfree(&nodr);
	regfree(&nodo);

	setmaxarg(n->left->type);
}

void
cgen_callmeth(Node *n, int proc)
{
	Node *l;

	// generate a rewrite for method call
	// (p.f)(...) goes to (f)(p,...)

	l = n->left;
	if(l->op != ODOTMETH)
		fatal("cgen_callmeth: not dotmethod: %N");

	n->op = OCALL;
	n->left = n->left->right;
	n->left->type = l->type;

	if(n->left->op == ONAME)
		n->left->class = PEXTERN;
	cgen_call(n, proc);
}

void
cgen_call(Node *n, int proc)
{
	Type *t;
	Node nod, afun, regax;

	if(n == N)
		return;

	if(n->left->ullman >= UINF) {
		// if name involves a fn call
		// precompute the address of the fn
		tempname(&afun, types[tptr]);
		if(isptr[n->left->type->etype])
			cgen(n->left, &afun);
		else
			agen(n->left, &afun);
	}

	gen(n->right);	// assign the args
	t = n->left->type;
	if(isptr[t->etype])
		t = t->type;

	setmaxarg(t);

	// call tempname pointer
	if(n->left->ullman >= UINF) {
		regalloc(&nod, types[tptr], N);
		cgen_as(&nod, &afun, 0);
		nod.type = t;
		ginscall(&nod, proc);
		regfree(&nod);
		goto ret;
	}

	// call pointer
	if(isptr[n->left->type->etype]) {
		regalloc(&nod, types[tptr], N);
		cgen_as(&nod, n->left, 0);
		nod.type = t;
		ginscall(&nod, proc);
		regfree(&nod);
		goto ret;
	}

	// call direct
	n->left->method = 1;
	ginscall(n->left, proc);

ret:
	;
}

void
cgen_proc(Node *n)
{
	switch(n->left->op) {
	default:
		fatal("cgen_proc: unknown call %O", n->left->op);

	case OCALLMETH:
		cgen_callmeth(n->left, 1);
		break;

	case OCALLINTER:
		cgen_callinter(n->left, N, 1);
		break;

	case OCALL:
		cgen_call(n->left, 1);
		break;
	}
		
}

void
cgen_callret(Node *n, Node *res)
{
	Node nod;
	Type *fp, *t;
	Iter flist;

	t = n->left->type;
	if(t->etype == TPTR32 || t->etype == TPTR64)
		t = t->type;

	fp = structfirst(&flist, getoutarg(t));
	if(fp == T)
		fatal("cgen_callret: nil");

	memset(&nod, 0, sizeof(nod));
	nod.op = OINDREG;
	nod.val.vval = D_SP;
	nod.addable = 1;

	nod.xoffset = fp->width;
	nod.type = fp->type;
	cgen_as(res, &nod, 0);
}

void
cgen_aret(Node *n, Node *res)
{
	Node nod1, nod2;
	Type *fp, *t;
	Iter flist;

	t = n->left->type;
	if(isptr[t->etype])
		t = t->type;

	fp = structfirst(&flist, getoutarg(t));
	if(fp == T)
		fatal("cgen_aret: nil");

	memset(&nod1, 0, sizeof(nod1));
	nod1.op = OINDREG;
	nod1.val.vval = D_SP;
	nod1.addable = 1;

	nod1.xoffset = fp->width;
	nod1.type = fp->type;

	gins(ALEAQ, &nod1, res);
}

void
cgen_ret(Node *n)
{
	gen(n->left);	// copy out args
	gins(ARET, N, N);
}

void
cgen_asop(Node *n)
{
	Node n1, n2, n3, n4;
	Node *nl, *nr;
	long lno;

	nl = n->left;
	nr = n->right;

	if(nr->ullman >= UINF && nl->ullman >= UINF) {
		tempname(&n1, nr->type);
		cgen(nr, &n1);
		n2 = *n;
		n2.right = &n1;
		cgen_asop(&n2);
		goto ret;
	}

	if(nr->ullman > nl->ullman) {
		regalloc(&n2, nl->type, N);
		cgen(nr, &n2);
		igen(nl, &n1, N);
	} else {
		igen(nl, &n1, N);
		regalloc(&n2, nl->type, N);
		cgen(nr, &n2);
	}

	n3 = *n;
	n3.left = &n1;
	n3.right = &n2;
	n3.op = n->etype;

	regalloc(&n4, nr->type, N);
	cgen(&n3, &n4);
	gmove(&n4, &n1);

	regfree(&n1);
	regfree(&n2);
	regfree(&n4);

ret:
	;
}

void
cgen_as(Node *nl, Node *nr, int op)
{
	Node nc, n1;
	Type *tl;
	ulong w, c;

	if(nl == N)
		return;

	tl = nl->type;
	if(tl == T)
		return;

	if(nr == N || isnil(nr)) {
		if(isfat(tl)) {
			/* clear a fat object */
			if(debug['g'])
				dump("\nclearfat", nl);

			w = nl->type->width;
			if(w > 0)
				gconreg(AMOVQ, 0, D_AX);

			if(w > 0) {
				nodreg(&n1, types[tptr], D_DI);
				agen(nl, &n1);
				gins(ACLD, N, N);	// clear direction flag
			}

			c = w / 8;
			if(c > 0) {
				gconreg(AMOVQ, c, D_CX);
				gins(AREP, N, N);	// repeat
				gins(ASTOSQ, N, N);	// STOQ AL,*(DI)+
			}

			c = w % 8;
			if(c > 0) {
				gconreg(AMOVQ, c, D_CX);
				gins(AREP, N, N);	// repeat
				gins(ASTOSB, N, N);	// STOB AL,*(DI)+
			}
			goto ret;
		}

		/* invent a "zero" for the rhs */
		nr = &nc;
		memset(nr, 0, sizeof(*nr));
		switch(tl->etype) {
		default:
			fatal("cgen_as: tl %T", tl);
			break;

		case TINT8:
		case TUINT8:
		case TINT16:
		case TUINT16:
		case TINT32:
		case TUINT32:
		case TINT64:
		case TUINT64:
			nr->val.ctype = CTINT;
			nr->val.vval = 0;
			break;

		case TFLOAT32:
		case TFLOAT64:
		case TFLOAT80:
			nr->val.ctype = CTFLT;
			nr->val.dval = 0.0;
			break;

		case TBOOL:
			nr->val.ctype = CTBOOL;
			nr->val.vval = 0;
			break;

		case TPTR32:
		case TPTR64:
			nr->val.ctype = CTNIL;
			nr->val.vval = 0;
			break;

//		case TINTER:
//			nodreg(&n1, types[tptr], D_DI);
//			agen(nl, &n1);
//			n1.op = OINDREG;
//
//			nodreg(&nc, types[tptr], D_AX);
//			gconreg(AMOVQ, 0, D_AX);
//
//			gins(AMOVQ, &nc, &n1);
//			n1.xoffset += widthptr;
//			gins(AMOVQ, &nc, &n1);
//			goto ret;

		}
		nr->op = OLITERAL;
		nr->type = tl;
		nr->addable = 1;
		ullmancalc(nr);
	}

	cgen(nr, nl);

ret:
	;
}

int
samereg(Node *a, Node *b)
{
	if(a->op != OREGISTER)
		return 0;
	if(b->op != OREGISTER)
		return 0;
	if(a->val.vval != b->val.vval)
		return 0;
	return 1;
}

/*
 * this is hard because divide
 * is done in a fixed numerator
 * of combined DX:AX registers
 */
void
cgen_div(int op, Node *nl, Node *nr, Node *res)
{
	Node n1, n2, n3;
	int a, rax, rdx;

	rax = reg[D_AX];
	rdx = reg[D_DX];

	nodreg(&n1, types[TINT64], D_AX);
	nodreg(&n2, types[TINT64], D_DX);
	regalloc(&n1, nr->type, &n1);
	regalloc(&n2, nr->type, &n2);

	// clean out the AX register
	if(rax && !samereg(res, &n1)) {
		regalloc(&n3, types[TINT64], N);
		gins(AMOVQ, &n1, &n3);
		regfree(&n1);
		regfree(&n2);

		reg[D_AX] = 0;
		cgen_div(op, nl, nr, res);
		reg[D_AX] = rax;

		gins(AMOVQ, &n3, &n1);
		regfree(&n3);
		goto ret;
	}

	// clean out the DX register
	if(rdx && !samereg(res, &n2)) {
		regalloc(&n3, types[TINT64], N);
		gins(AMOVQ, &n2, &n3);
		regfree(&n1);
		regfree(&n2);

		reg[D_DX] = 0;
		cgen_div(op, nl, nr, res);
		reg[D_DX] = rdx;

		gins(AMOVQ, &n3, &n2);
		regfree(&n3);
		goto ret;
	}

	a = optoas(op, nl->type);

	if(!issigned[nl->type->etype]) {
		nodconst(&n3, nl->type, 0);
		gmove(&n3, &n2);
	}

	regalloc(&n3, nr->type, N);
	if(nl->ullman >= nr->ullman) {
		cgen(nl, &n1);
		if(issigned[nl->type->etype])
			gins(optoas(OFOR, nl->type), N, N);
		cgen(nr, &n3);
		gins(a, &n3, N);
	} else {
		cgen(nr, &n3);
		cgen(nl, &n1);
		if(issigned[nl->type->etype])
			gins(optoas(OFOR, nl->type), N, N);
		gins(a, &n3, N);
	}
	regfree(&n3);

	if(op == ODIV)
		gmove(&n1, res);
	else
		gmove(&n2, res);

	regfree(&n1);
	regfree(&n2);

ret:
	;
}

/*
 * this is hard because shift
 * count is either constant
 * or the CL register
 */
void
cgen_shift(int op, Node *nl, Node *nr, Node *res)
{
	Node n1, n2;
	int a, rcl;

	a = optoas(op, nl->type);

	if(nr->op == OLITERAL) {
		regalloc(&n1, nr->type, res);
		cgen(nl, &n1);
		gins(a, nr, &n1);
		gmove(&n1, res);
		regfree(&n1);
		goto ret;
	}

	rcl = reg[D_CX];

	nodreg(&n1, types[TINT64], D_CX);
	regalloc(&n1, nr->type, &n1);

	// clean out the CL register
	if(rcl && !samereg(res, &n1)) {
		regalloc(&n2, types[TINT64], N);
		gins(AMOVQ, &n1, &n2);
		regfree(&n1);

		reg[D_CX] = 0;
		cgen_shift(op, nl, nr, res);
		reg[D_CX] = rcl;

		gins(AMOVQ, &n2, &n1);
		regfree(&n2);
		goto ret;
	}

	regalloc(&n2, nl->type, res);	// can one shift the CL register?
	if(nl->ullman >= nr->ullman) {
		cgen(nl, &n2);
		cgen(nr, &n1);
	} else {
		cgen(nr, &n1);
		cgen(nl, &n2);
	}
	gins(a, &n1, &n2);
	gmove(&n2, res);

	regfree(&n1);
	regfree(&n2);

ret:
	;
}
