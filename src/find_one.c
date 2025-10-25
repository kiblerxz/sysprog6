// src/find_one.c
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const int *a;
    int n, val, tid, nt;
    int first;              // 1=first, 0=last
    const char *sync;       // "mutex"|"spin"|"barrier"
} ctx_t;

static pthread_mutex_t g_mx;
static pthread_spinlock_t g_spin;
static int g_result;        // глобальный индекс результата (для mutex/spin)
static int *local_res;      // для barrier: один результат на поток, затем агрегируем

static void* worker_mutex(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    int best = -1;
    if (c->first) {
        for (int i=from;i<to;i++) if (c->a[i]==c->val){ best=i; break; }
        if (best!=-1) {
            pthread_mutex_lock(&g_mx);
            if (g_result==-1 || best < g_result) g_result = best;
            pthread_mutex_unlock(&g_mx);
        }
    } else {
        for (int i=to-1;i>=from;i--) if (c->a[i]==c->val){ best=i; break; }
        if (best!=-1) {
            pthread_mutex_lock(&g_mx);
            if (g_result==-1 || best > g_result) g_result = best;
            pthread_mutex_unlock(&g_mx);
        }
    }
    return NULL;
}

static void* worker_spin(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    int best = -1;
    if (c->first) {
        for (int i=from;i<to;i++) if (c->a[i]==c->val){ best=i; break; }
        if (best!=-1) {
            pthread_spin_lock(&g_spin);
            if (g_result==-1 || best < g_result) g_result = best;
            pthread_spin_unlock(&g_spin);
        }
    } else {
        for (int i=to-1;i>=from;i--) if (c->a[i]==c->val){ best=i; break; }
        if (best!=-1) {
            pthread_spin_lock(&g_spin);
            if (g_result==-1 || best > g_result) g_result = best;
            pthread_spin_unlock(&g_spin);
        }
    }
    return NULL;
}

static pthread_barrier_t g_bar; // для barrier-варианта

static void* worker_barrier(void *arg){
    ctx_t *c = (ctx_t*)arg;
    int from = (long long)c->tid * c->n / c->nt;
    int to   = (long long)(c->tid+1) * c->n / c->nt;
    int best = -1;
    if (c->first) {
        for (int i=from;i<to;i++) if (c->a[i]==c->val){ best=i; break; }
    } else {
        for (int i=to-1;i>=from;i--) if (c->a[i]==c->val){ best=i; break; }
    }
    local_res[c->tid] = best;
    pthread_barrier_wait(&g_bar);
    return NULL;
}

static void usage(const char* prog){
    fprintf(stderr,
        "Usage: %s [--first|--last] --sync=mutex|spin|barrier [-t THREADS]\n"
        "Reads: n, then n ints, then value\n", prog);
}

int main(int argc, char**argv){
    int first = 1;
    const char *sync="mutex";
    int nt=4;

    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--first")) first=1;
        else if (!strcmp(argv[i],"--last")) first=0;
        else if (!strncmp(argv[i],"--sync=",7)) sync=argv[i]+7;
        else if (!strcmp(argv[i],"-t") && i+1<argc) nt=atoi(argv[++i]);
        else { usage(argv[0]); return 2; }
    }

    int n; if (scanf("%d",&n)!=1 || n<=0){ fprintf(stderr,"bad n\n"); return 1; }
    int *a = malloc((size_t)n*sizeof(int));
    if(!a){ perror("malloc"); return 1; }
    for(int i=0;i<n;i++) if (scanf("%d",&a[i])!=1){ fprintf(stderr,"bad arr\n"); free(a); return 1; }
    int val; if (scanf("%d",&val)!=1){ fprintf(stderr,"bad val\n"); free(a); return 1; }

    if (nt<1) nt=1;
    pthread_t *th = malloc((size_t)nt*sizeof(*th));
    ctx_t *ctx = calloc((size_t)nt, sizeof(*ctx));
    if(!th||!ctx){ perror("oom"); free(a); free(th); free(ctx); return 1; }

    for (int t=0;t<nt;t++){
        ctx[t]=(ctx_t){ .a=a,.n=n,.val=val,.tid=t,.nt=nt,.first=first,.sync=sync };
    }

    if (!strcmp(sync,"mutex")){
        pthread_mutex_init(&g_mx, NULL);
        g_result = -1;
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_mutex,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_mutex_destroy(&g_mx);
        printf("%d\n", g_result);
    } else if (!strcmp(sync,"spin")){
        pthread_spin_init(&g_spin, 0);
        g_result = -1;
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_spin,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_spin_destroy(&g_spin);
        printf("%d\n", g_result);
    } else if (!strcmp(sync,"barrier")){
        pthread_barrier_init(&g_bar, NULL, nt);
        local_res = malloc((size_t)nt * sizeof(int));
        for(int i=0;i<nt;i++) local_res[i]=-1;
        for (int t=0;t<nt;t++) pthread_create(&th[t],NULL,worker_barrier,&ctx[t]);
        for (int t=0;t<nt;t++) pthread_join(th[t],NULL);
        pthread_barrier_destroy(&g_bar);
        int ans = -1;
        if (first){
            for(int i=0;i<nt;i++) if(local_res[i]!=-1 && (ans==-1 || local_res[i]<ans)) ans=local_res[i];
        }else{
            for(int i=0;i<nt;i++) if(local_res[i]!=-1 && (ans==-1 || local_res[i]>ans)) ans=local_res[i];
        }
        printf("%d\n", ans);
        free(local_res);
    } else {
        usage(argv[0]); free(a); free(th); free(ctx); return 2;
    }

    free(a); free(th); free(ctx);
    return 0;
}
