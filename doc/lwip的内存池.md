# Lwip的内存池

``` cpp
/* /lwip/include/lwip/priv/memp_priv.h */
/** Memory pool descriptor */
struct memp_desc {
  /** Element size */
  u16_t size;

  /** Number of elements */
  /** 内存块的数量 */
  u16_t num;

  /** Base address */
  /** 内存池的起始地址 */
  u8_t *base;

  /** First free element of each pool. Elements form a linked list. */
  struct memp **tab;

};

/** 链表将所有的空闲块连起来 */
struct memp {
  struct memp *next;
};
```

``` cpp
/**
 * 内存池的初始化函数
 * Initializes lwIP built-in pools.
 * Related functions: memp_malloc, memp_free
 *
 * Carves out memp_memory into linked lists for each pool-type.
 */
void
memp_init(void)
{
  u16_t i;

  /* for every pool: */
  for (i = 0; i < LWIP_ARRAYSIZE(memp_pools); i++) {
    memp_init_pool(memp_pools[i]);
  }
}

/**
 * Initialize custom memory pool.
 * Related functions: memp_malloc_pool, memp_free_pool
 *
 * @param desc pool to initialize
 */
void
memp_init_pool(const struct memp_desc *desc)
{
  int i;
  struct memp *memp;

  *desc->tab = NULL;
  memp = (struct memp *)LWIP_MEM_ALIGN(desc->base);
  /* create a linked list of memp elements */
  for (i = 0; i < desc->num; ++i) {
    memp->next = *desc->tab;
    *desc->tab = memp;
    
    /* cast through void* to get rid of alignment warnings */
    memp = (struct memp *)(void *)((u8_t *)memp + MEMP_SIZE + desc->size);
  }
}


```
