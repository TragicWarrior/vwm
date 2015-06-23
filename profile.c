#include <unistd.h>
#include <pwd.h>
#include <string.h>

#include <sys/stat.h>

#include "vwm.h"

VWM_PROFILE* vwm_profile_init(void)
{
   static VWM_PROFILE   *vwm_profile=NULL;
   struct passwd        *user_info;
   struct stat          stat_info;
   gchar                *buffer;
   gint                 i;

   if(vwm_profile!=NULL) return vwm_profile;

   vwm_profile=(VWM_PROFILE*)g_malloc0(sizeof(VWM_PROFILE));
   vwm_profile->user=getuid();

   user_info=getpwuid(vwm_profile->user);
   if(user_info!=NULL)
   {
      vwm_profile->login=g_strdup(user_info->pw_name);
      vwm_profile->passwd=g_strdup(user_info->pw_passwd);
      vwm_profile->home=g_strdup(user_info->pw_dir);
   }   

   /* try to get home dir from environment if getpwuid()->pw_passed fails. */
   if(vwm_profile->home==NULL) vwm_profile->home=g_strdup(getenv("HOME"));

   /* make sure home directory path is formatted correctly. */  
   i=strlen(vwm_profile->home);
   do
   {
      if(i<2) break;
      if(vwm_profile->home[i-1]=='/') vwm_profile->home[i-1]='\0';
      i=strlen(vwm_profile->home);
   }
   while(vwm_profile->home[i-1]=='/');
   
   /* check to see if ~/.vwm/vwmrc config exists. */
   buffer=g_strdup_printf("%s/.vwm/vwmrc",vwm_profile->home);
   if(stat(buffer,&stat_info)==0)
   {
      if(S_ISREG(stat_info.st_mode)) vwm_profile->rc_file=buffer;
      else g_free(buffer);
   }
   else g_free(buffer);

   /* check to see if ~/.vwm/modules directory exists. */
   buffer=g_strdup_printf("%s/.vwm/modules",vwm_profile->home);
   if(stat(buffer,&stat_info)==0)
   {
      if(S_ISDIR(stat_info.st_mode)) vwm_profile->mod_dir=buffer;
      else g_free(buffer);
   }
   else g_free(buffer);

   return vwm_profile;
}
      
gchar* vwm_profile_mod_dir_get()
{
   VWM_PROFILE *vwm_profile;

   vwm_profile=vwm_profile_acquire();
   return vwm_profile->mod_dir;
}

gchar* vwm_profile_login_get()
{
   VWM_PROFILE *vwm_profile;

   vwm_profile=vwm_profile_acquire();
   return vwm_profile->login;
}

gchar* vwm_profile_rc_file_get()
{
   VWM_PROFILE *vwm_profile;

   vwm_profile=vwm_profile_acquire();
   return vwm_profile->rc_file;
}  
