#include "cache.h"

static sem_t mutex;

// make cache have enough size to store new cache
static int clear_n_cache(cache *ca, unsigned int needsize)
{
    LOG("start ...");
    int freesize = MAX_CACHE_SIZE - ca->size_all;
    struct cache_node *node;
    if (freesize >= needsize)
    {
        return SUCCESS;
    }

    while (freesize >= needsize) 
    {
        node = ca->tail;
        freesize += node->size;
        
        // remove cache
        ca->tail = node->prev;
        ca->tail->next = NULL;
        if (node->uri)
            free(node->uri);
        if (node->content)
            free(node->content);
        ca->size_all -= node->size;
        free(node);
    }
    LOG("end...");
    return SUCCESS;
}


static int insert_cache_node(cache *ca, struct cache_node *node)
{
    if (ca->head == NULL) 
    {
        ca->head = node;
        ca->tail = node;
    }
    else 
    {
        node->prev = ca->tail;
        ca->tail->next = node;
        ca->tail = node;
    }

    ca->size_all += node->size;

    return SUCCESS;
}


static int create_cache_node(struct cache_node *node, char *buf, char *uri, unsigned int size)
{
    int ret = SUCCESS;
    char *uricopy = NULL;
    char *content = NULL;
    uricopy = (char *)malloc(strlen(uri) + 1);
    if (uricopy == NULL)
    {
        fprintf(stderr, "%s malloc uri failed.\n", __FUNCTION__);
        ret = ERROR_MALLOC_FAIL;
        goto _quit;
    }
    memcpy(uricopy, uri, strlen(uri) + 1);

    content = (char *)malloc(size);
    if (content == NULL)
    {
        fprintf(stderr, "%s malloc content failed.\n", __FUNCTION__);
        ret = ERROR_MALLOC_FAIL;
        goto _quit;
    }
    memcpy(content, buf, size);
    
    node->uri = uricopy;
    node->content = content;
    node->size = size;
    node->next = NULL;
    node->prev = NULL;
    return SUCCESS;

_quit:
    if (content)
        free(content);
    if (uricopy)
        free(uricopy);
    return ret;
}

int init_cache(cache *ca)
{
    memset(ca, 0, sizeof(cache));
    dbg_printf("%s success\n", __FUNCTION__);
    Sem_init(&mutex, 0, 1);
    return SUCCESS;
}

int find_cache(cache *ca, char *uri, char *bufout, unsigned int *psize)
{
    unsigned int size = *psize;
    int ret = SUCCESS;
    // P
    P(&mutex);
    struct cache_node *node = ca->head;
    
    while (node)
    {
        if (!strcmp(node->uri, uri))
        {
            break;
        }
        node = node->next;
    }

    if (node != NULL) // find
    {
        if (node->size <= size) 
        {
            memcpy(bufout, node->content, node->size);
            *psize = node->size;
            // TODO move node to head

        }   
        else
        {
            ret = ERROR_BUFF_TOO_SMALL;
        }
    } 
    else // not find
    {
        ret = ERROR_CACHE_FIND_NOTHING;
    }

    // V
    V(&mutex);
    return ret;
}

int add_cache(cache *ca, char *uri, char *bufin, unsigned int size)
{
    if (size > MAX_OBJECT_SIZE) 
    {
        return ERROR_ADD_CACHE_OVERSIZE;
    }
    int ret = SUCCESS;
    struct cache_node *newnode;


    // create new node
    newnode = (struct cache_node *)malloc(sizeof(struct cache_node));
    if (newnode == NULL)
    {
        fprintf(stderr, "%s malloc cache node failed.\n", __FUNCTION__);
        ret = ERROR_MALLOC_FAIL;
        goto _quit;
    }
    if ((ret = create_cache_node(newnode, bufin, uri, size)) != SUCCESS) 
    {
        goto _quit;
    }

    // P 
    P(&mutex);
    ret = clear_n_cache(ca, size);
    // insert new node
    insert_cache_node(ca, newnode);
    // V 
    V(&mutex);

    return SUCCESS;
_quit:
    if (newnode)
        free(newnode);

    return ret;
}

int clear_all_cache(cache *ca)
{
    struct cache_node *node, *temp;
    P(&mutex);
    node = ca->head;
    while (node)
    {
        temp = node;
        node = node->next;

        // free temp
        if (temp->uri)
            free(temp->uri);
        if (temp->content)
            free(temp->content);
        free(temp);
    }

    ca->size_all = 0;
    ca->head = NULL;
    ca->tail = NULL;
    V(&mutex);
    return SUCCESS;
}

int deinit_cahce(cache *ca)
{
    int ret;
    ret = clear_all_cache(ca);
    if (ret != SUCCESS) 
    {
        return ret;
    }

    memset(ca, 0, sizeof(cache));
    return SUCCESS;
}

