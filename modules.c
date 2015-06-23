#include <dirent.h>
#include <string.h>
#include <dlfcn.h>

#include <sys/types.h>

#include <pseudo.h>

#include <glib.h>

#include "vwm.h"
#include "modules.h"
#include "profile.h"


static int _vwm_module_init(const char *);

vwm_module_t*
vwm_module_create(void)
{
    vwm_module_t    *module;

    module = (vwm_module_t*)calloc(1,sizeof(vwm_module_t));

    return module;
}

void
vwm_module_set_type(vwm_module_t *mod,int type)
{
    if(mod == NULL) return;

    mod->type = type;

    return;
}

int
vwm_module_get_type(vwm_module_t *mod)
{
    if(mod == NULL) return -1;

    return mod->type;
}

void
vwm_module_set_title(vwm_module_t *mod,char *title)
{
    if(mod == NULL) return;
    if(title == NULL) return;

    memset(mod->title,0,NAME_MAX);
    strncpy(mod->title,title,NAME_MAX - 1);

    return;
}

void
vwm_module_get_title(vwm_module_t *mod,char *buf,int buf_sz)
{
    if(mod == NULL) return;
    if(buf == NULL) return;
    if(buf_sz < 1) return;

    if(buf_sz > NAME_MAX - 1) buf_sz = NAME_MAX - 1;

    memset(buf,0,buf_sz);
    strncpy(buf,mod->title,buf_sz - 1);

    return;
}

WINDOW*
vwm_module_exec(vwm_module_t *mod)
{
    WINDOW  *window;

    if(mod == NULL) return NULL;

    if(mod->main == NULL) return NULL;

    window = mod->main(mod);

    return window;
}


gchar*
vwm_modules_load(char *module_dir)
{
    vwm_module_t    *vwm_module;
	char			modpath[NAME_MAX];
	DIR				*search_dir = NULL;
	struct dirent	*dirent_file = NULL;
	GSList			*module_list = NULL;
	GSList			*node;
    gchar           *buffer = NULL;
    gchar           *error_msg = NULL;
    int             retval;

    if(module_dir == NULL) return NULL;
        search_dir = opendir(module_dir);
    if(search_dir == NULL)
    {
        buffer = g_strdup_printf("error opening module directory:\n%s",
            module_dir);

        return buffer;
    }

	module_list = vwm_modules_list(-1);

	while((dirent_file = readdir(search_dir)) != NULL)
	{
		if(strcmp(dirent_file->d_name,".") == 0) continue;
		if(strcmp(dirent_file->d_name,"..") == 0) continue;

		// fix module file name (maybe not necessary)
		if(module_dir[strlen(module_dir) - 1] == '/')
			sprintf(modpath,"%s%s",module_dir,dirent_file->d_name);
		else
			sprintf(modpath,"%s/%s",module_dir,dirent_file->d_name);

		// check to see if module is already registered
		node = module_list;
		while(node != NULL)
		{
			vwm_module = (vwm_module_t*)node->data;
			if(strstr(vwm_module->modpath,modpath) != NULL) break;

			node = node->next;
		}

		// module isn't already registered
		if(node == NULL)
        {
            retval = _vwm_module_init(modpath);

            if(retval != 0)
            {
                switch(retval)
                {
                    case -1:    error_msg = dlerror();                  break;
                    case -2:    error_msg = "vwm_mod_main() not found"; break;
                    default:    error_msg = "error from module init";   break;
                }

                if(error_msg != NULL) buffer = g_strdup_printf("%s\n%s\n\n%s",
                    "Could not register module:",modpath,error_msg);
                else
                    buffer = g_strdup_printf("%s\n%s\n",
                    "Could not register module:",modpath);
            }
        }
    }

    closedir(search_dir);

	/*	obligatory clean up	*/
	if(module_list != NULL) g_slist_free(module_list);
    if(buffer != NULL) return buffer;

	return NULL;
}

int
vwm_module_add(const vwm_module_t *mod)
{
	VWM			    *vwm;
    GSList          *node;
    vwm_module_t    *node_item;

    if(mod->type == 0) return -1;
    if(mod->title == '\0') return -1;
    if(mod->modpath == '\0') return -1;

	if(strlen(mod->modpath) > NAME_MAX - 1) return -1;
	vwm = vwm_get_instance();

	// make sure app is not already loaded
	node = vwm->module_list;
	while(node != NULL)
	{
		node_item = (vwm_module_t*)node->data;

        // quick check
		if(node_item->main == mod->main)
        {
            // full check
            if(strncmp(node_item->modpath,mod->modpath,NAME_MAX) == 0)
            {
                // if the module already exists, return -2 so that
                // _vwm_module_init will free the memory
                return -2;
            }
        }

		node = node->next;
	}

	// add the application to the list
	vwm->module_list = g_slist_prepend(vwm->module_list,(gpointer)mod);

	return 0;
}

GSList*
vwm_modules_list(int type)
{
	VWM		    	*vwm;
	vwm_module_t	*node_item;
	GSList		    *list_copy = NULL;
	GSList		    *node;

	vwm = vwm_get_instance();
	if(g_slist_length(vwm->module_list) == 0) return NULL;

	if(type == -1)
	{
		list_copy = g_slist_copy(vwm->module_list);
		return list_copy;
	}

	node = vwm->module_list;
	while(node != NULL)
	{
		node_item = (vwm_module_t*)node->data;
		if(node_item->type == type)
			list_copy = g_slist_prepend(list_copy,node->data);

		node = node->next;
	}

	return list_copy;
}

vwm_module_t*
vwm_module_find_by_title(char *title)
{
	VWM			    *vwm;
	vwm_module_t	*node_item;
	GSList		    *node;

	if(title == NULL) return NULL;
	vwm = vwm_get_instance();

	node = vwm->module_list;
	while(node != NULL)
	{
		node_item = (vwm_module_t*)node->data;
		if(strcmp(node_item->title,title) == 0) break;
		node = node->next;
	}

	if(node == NULL) return NULL;

	return node_item;
}

vwm_module_t*
vwm_module_find_by_type(vwm_module_t *prev_mod,int type)
{
    VWM             *vwm;

    vwm_module_t    *node_item = NULL;
    GSList          *node;

    if(type < 0) return NULL;

    vwm = vwm_get_instance();
    node = vwm->module_list;

    // check to see if this is a subsequent search
    if(prev_mod != NULL)
    {
        while(node != NULL)
        {
            node_item = (vwm_module_t*)node->data;
            node = node->next;

            if(node_item == prev_mod)
            {
                node_item = NULL;
                break;
            }
        }
    }

    while(node != NULL)
    {
        node_item = (vwm_module_t*)node->data;

        if(node_item->type == type) break;

        node = node->next;
    }

    if(node == NULL) return NULL;

    return node_item;
}


static int
_vwm_module_init(const char *modpath)
{
    // extern char     **vwm_argv;
    // extern int      vwm_argc;

    void            *handle = NULL;
    vwm_module_t    *mod = NULL;
    int             retval = 0;
    int             (*constructor)(vwm_module_t *);

    handle = dlopen(modpath,RTLD_LAZY | RTLD_LOCAL);

    if(handle == NULL) return -1;

    *(void **)(&constructor) = dlsym(handle,"vwm_mod_init");

    // return error if the module constructor could not be mapped
    if(constructor == NULL)
    {
        dlclose(handle);
        return -2;
    }

    mod = (vwm_module_t*)calloc(1,sizeof(vwm_module_t));
    strncpy(mod->modpath,modpath,NAME_MAX - 1);

    // call the module constructor
    retval = constructor(mod);

    // handle "user error" from module constructor
    if(retval != 0)
    {
        free(mod);
        dlclose(handle);
        return -3;
    }

    return 0;
}
