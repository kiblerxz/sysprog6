// src/find_all.c
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const int *a;
    int n, val, tid, nt;
    const char *sync;      // "mutex"|"spin"|"barrier"
    int order_asc;         // 1=asc, 0=desc
} ctx_t;

static pthread_mutex_t g_mx;
static pthread_spinlock_t g_spin;

typedef struct { int *v; int sz, cap; } vec_t;
static void vec_init(vec_t *x){ x->v=NULL; x->sz=0; x->cap=0; }
static void vec_push(vec_t *x, int val){
    if (x->sz==x->cap){ x->cap = x->cap? x->cap*2:16; x->v = realloc(x->v, (size_t)x->cap*sizeof(int)); if(!x->v){perror("realloc"); exit(1);} }
    x->v[x->sz++] = val;
}
static int cmp_asc (const void* a,const void* b){ int x=*(const int*)a, y=*(const int*)b; return (x>y)-(x<y); }
static int cmp_desc(const void* a,const void* b){ int x=*(const int*)a, y=*(const int*)b; return (y>x)-(y<x); }

static vec_t g_indices;             // общая коллекция (mutex/spin)
static pthread_barrier_t g_bar;
static vec_t *local_lists;          // для barrier: на поток

static void* worker_mutex(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    for (int i=from;i<to;i++){
        if (c->a[i]==c->val){
            pthread_mutex_lock(&g_mx);
            vec_push(&g_indices, i);
            pthread_mutex_unlock(&g_mx);
        }
    }
    return NULL;
}

static void* worker_spin(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    for (int i=from;i<to;i++){
        if (c->a[i]==c->val){
            pthread_spin_lock(&g_spin);
            vec_push(&g_indices, i);
            pthread_spin_unlock(&g_spin);
        }
    }
    return NULL;
}

static void* worker_barrier(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    vec_t *L = &local_lists[c->tid];
    vec_init(L);
    for (int i=from;i<to;i++) if (c->a[i]==c->val) vec_push(L, i);
    pthread_barrier_wait(&g_bar);
    return NULL;
}

static void usage(const char* prog){
    fprintf(stderr,
        "Usage: %s --order=asc|desc --sync=mutex|spin|barrier [-t THREADS]\n"
        "Reads: n, then n ints, then value\n", prog);
}

int main(int argc, char **argv){
    const char *sync="mutex";
    int order_asc=1, nt=4;

    for (int i=1;i<argc;i++){
        if (!strncmp(argv[i],"--order=",8)){
            const char *o=argv[i]+8; order_asc = !strcmp(o,"asc") ? 1 : (!strcmp(o,"desc") ? 0 : -1);
        } else if (!strncmp(argv[i],"--sync=",7)) sync=argv[i]+7;
        else if (!strcmp(argv[i],"-t") && i+1<argc) nt=atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }
    if (order_asc==-1){ usage(argv[0]); return 2; }

    int n; if (scanf("%d",&n)!=1 || n<=0){ fprintf(stderr,"bad n\n"); return 1; }
    int *a = malloc((size_t)n*sizeof(int));
    if(!a){ perror("malloc"); return 1; }
    for(int i=0;i<n;i++) if (scanf("%d",&a[i])!=1){ fprintf(stderr,"bad arr\n"); free(a); return 1; }
    int val; if (scanf("%d",&val)!=1){ fprintf(stderr,"bad val\n"); free(a); return 1; }

    if (nt<1) nt=1;
    pthread_t *th = malloc((size_t)nt*sizeof(*th));
    ctx_t *ctx = calloc((size_t)nt, sizeof(*ctx));
    if(!th||!ctx){ perror("oom"); free(a); free(th); free(ctx); return 1; }
    for (int t=0;t<nt;t++) ctx[t]=(ctx_t){ .a=a,.n=n,.val=val,.tid=t,.nt=nt,.sync=sync,.order_asc=order_asc };

    if (!strcmp(sync,"mutex")){
        pthread_mutex_init(&g_mx, NULL);
        vec_init(&g_indices);
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_mutex,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_mutex_destroy(&g_mx);

        qsort(g_indices.v, (size_t)g_indices.sz, sizeof(int), order_asc?cmp_asc:cmp_desc);
        for (int i=0;i<g_indices.sz;i++) printf("%d ", g_indices.v[i]);
        printf("\n");
        free(g_indices.v);

    } else if (!strcmp(sync,"spin")){
        pthread_spin_init(&g_spin, 0);
        vec_init(&g_indices);
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_spin,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_spin_destroy(&g_spin);

        qsort(g_indices.v, (size_t)g_indices.sz, sizeof(int), order_asc?cmp_asc:cmp_desc);
        for (int i=0;i<g_indices.sz;i++) printf("%d ", g_indices.v[i]);
        printf("\n");
        free(g_indices.v);

    } else if (!strcmp(sync,"barrier")){
        pthread_barrier_init(&g_bar, NULL, nt);
        local_lists = calloc((size_t)nt, sizeof(vec_t));
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_barrier,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_barrier_destroy(&g_bar);

        int total=0; for(int t=0;t<nt;t++) total += local_lists[t].sz;
        int *all = total? (int*)malloc((size_t)total*sizeof(int)) : NULL;
        int k=0; for(int t=0;t<nt;t++){ for(int i=0;i<local_lists[t].sz;i++) all[k++]=local_lists[t].v[i]; free(local_lists[t].v); }
        free(local_lists);

        if (total){
            qsort(all, (size_t)total, sizeof(int), order_asc?cmp_asc:cmp_desc);
            for(int i=0;i<total;i++) printf("%d ", all[i]);
            printf("\n");
            free(all);
        } else {
            printf("\n");
        }
    } else {
        usage(argv[0]); free(a); free(th); free(ctx); return 2;
    }

    free(a); free(th); free(ctx);
    return 0;
}
