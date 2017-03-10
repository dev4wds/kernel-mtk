/* devinfo_flash.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/* 
 * private documents from Longcheer (LCT) software.
 * This is designed for collecting information of eMCP,eMMC,lpSRAM. etc.
 * By   : shaohui@longcheer.net
 * Date : 2013/1/1
 *
 * */
/*=================================================================*/
/*
*	Date	: 2016.06.12
*	By		: shaohui
*	Discription:
*	Because since android L,Mediatek delete msdc_check_init_done(),So 
*	it's difficult to get a completed sign for ROM capacity caculation. 
*	I had to add a workqueue to start a work, if a certain condition 
*	was satisfied.
*/
/*=================================================================*/
/*=================================================================*/
/*
*	Date	: 2016.11.1
*	By		: shaohui
*	Discription:
*	Fix bugs for Dis components.
*
*/
/*=================================================================*/
/*=================================================================*/
/*
*	Date	: 2016.11.7
*	By		: shaohui
*	Discription:
*	Add proc to get ramsize
*
*/
/*=================================================================*/



#include <linux/kernel.h>	/* printk()*/
#include <linux/module.h>
#include <linux/types.h>	/* size_t */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/proc_fs.h>  /*proc*/
#include <linux/genhd.h>    //special
#include <linux/cdev.h>
#include <asm/uaccess.h>   /*set_fs get_fs mm_segment_t*/
#include <linux/delay.h>

//#include <mt-plat/sd_misc.h>
#include "mtk_sd.h"

#define LCT_DEVINFO_DEBUG_EMCP 1


extern void msdc_check_init_done(void);
extern struct msdc_host *mtk_msdc_host[];

//////////////////////shaohui add for eMCP info//////////////////
#include "dev_info.h"
//#include "devinfo_emi.h"

#include "lct_print.h"
#ifdef LCT_LOG_MOD
#undef LCT_LOG_MOD
#define LCT_LOG_MOD "[DEVINFO][emcp]" 
#endif


#define MAX_DEV_LIST 20
static struct devinfo_struct *dev_list[MAX_DEV_LIST];
static unsigned int rom_size=0;
static unsigned long ram_size=0;

static unsigned long inforam_size=0;

#ifdef DEVINFO_DIS_DDR_STATUS
static unsigned int boot_ram_list_num = 0xff;
static void get_boot_ram_listnum(void)
{
	char *ptr;
	ptr = strstr(saved_command_line,"srambootnum=");
	ptr += strlen("srambootnum=");
	boot_ram_list_num = simple_strtol(ptr,NULL,10);
#ifdef LCT_DEVINFO_DEBUG_EMCP
    lct_pr_debug("[%s]: Get boot ram list num form comandline.[%d]\n", __func__,boot_ram_list_num);
#endif
}
#endif



// add by zhaofei - 2017-01-05-14-21
typedef struct{
	char *vendor_name;
	char *cid;
	char ram_size;
}lct_emmc_info;

static lct_emmc_info lct_emmc_info_str[] = {
	{"KMRX1000BM_B614",		"150100525831424D42", 3},
	{"KMGX6001BM_B514",		"150100475836424d42", 3},
	{"H9TQ26ADFTBCUR_KUM",	"90014A484247346132", 3},
	{"KMQE60013M_B318",		"150100514536334D42", 2},
	{"H9TQ17ABJTBCUR_KUM",	"90014a484147346132", 2},
};

static unsigned int atohex(char *str)
{
	unsigned int hex = 0;
	printk("<%s:%d>str[%s]\n", __func__, __LINE__, str);
	if (str == NULL)
		return 0;
	if (strlen(str) > 8)
		return 0;
	while(*str != '\0')
	{
		hex = hex * 16;
		if ((*str >= '0') && (*str <= '9'))
			hex += *str - '0';
		if ((*str >= 'a') && (*str <= 'f'))
			hex += *str - 'a';
		if ((*str >= 'A') && (*str <= 'F'))
			hex += *str - 'A';
		//hex = hex * 16;
		str++;	
	};
	printk("<%s:%d>hex[%x]\n", __func__, __LINE__, hex);
	return hex;
}
extern unsigned char cid_str[sizeof(u32)*4+1];
#define DEVINFO_EMMC_LIST_SIZE (sizeof(lct_emmc_info_str)/sizeof(lct_emmc_info))


static int devinfo_register_emcp(struct msdc_host *host)
{	
	int i=0;
	//int j=0;
//	char* type;
//	char* module;
//	char* vendor;
//	char *ic;
//	char *version = DEVINFO_NULL;
//	char *info=kmalloc(64,GFP_KERNEL);	//RAM SIZE
	char cid_t;
	char cid_tran_temp[10];
	char *info1=kmalloc(sizeof(char*),GFP_KERNEL);	//RAM SIZE
#ifdef SYSFS_INFO_SUPPORT
	char *coments1=kmalloc(sizeof(char*),GFP_KERNEL);	//RAM SIZE
	//char *emmc_fw=kmalloc(sizeof(char*),GFP_KERNEL);	//emmc FW version
#endif
#ifdef LCT_DEVINFO_DEBUG_EMCP
    //lct_pr_debug("[%s]: register emcp info.count number:[%d]\n", __func__,num_of_emi_records);
    lct_pr_debug("[%s]: register emcp info.device type:[%x]\n", __func__,host->mmc->card->type);
    lct_pr_debug("emcp device info.[%x][%x][%x][%x]\n",host->mmc->card->raw_cid[0],host->mmc->card->raw_cid[1],host->mmc->card->raw_cid[2],host->mmc->card->raw_cid[3]);
    lct_pr_debug("emcp device CID info.[%x][%s][%x][%x]\n",host->mmc->card->cid.manfid,host->mmc->card->cid.prod_name,host->mmc->card->cid.fwrev,host->mmc->card->cid.hwrev);
	lct_pr_debug(" size of emmc list:%d\n",(int)DEVINFO_EMMC_LIST_SIZE);
#endif

	for(i=0;i<DEVINFO_EMMC_LIST_SIZE;i++)
	{
  	dev_list[i] = (struct devinfo_struct*)kmalloc(sizeof(struct devinfo_struct), GFP_KERNEL);
//	memset(dev_list[i],0,sizeof(struct devinfo_struct));
#ifdef LCT_DEVINFO_DEBUG_EMCP
    //lct_pr_debug("emcp list info.[%x][%x][%x][%x][%x][%x][%x][%x]\n",emi_settings[i].ID[0],emi_settings[i].ID[1],emi_settings[i].ID[2],emi_settings[i].ID[3],emi_settings[i].ID[4],emi_settings[i].ID[5],emi_settings[i].ID[6],emi_settings[i].ID[7]);
#endif

		
		//type = "RAM DDR2      "";
		dev_list[i]->device_type=	"eMCP(eMMC+DDR3)";
		//device module
			dev_list[i]->device_module=	lct_emmc_info_str[i].vendor_name;
		#ifdef LCT_DEVINFO_DEBUG_EMCP
    	lct_pr_debug("device type:%s!\n",dev_list[i]->device_type);
    	lct_pr_debug("device module:%s!\n",dev_list[i]->device_module);
		#endif
    	memcpy(cid_tran_temp, lct_emmc_info_str[i].cid, 2);
    	cid_tran_temp[2] = '\0';	
    	cid_t = atohex(cid_tran_temp);
		//device vendor,eMMC PART
		//For dist ddr,it is NULL in emi_setting.ID[n],we`d better get it from what we have got
		
			switch(cid_t)
			{
				case 0x15:
					dev_list[i]->device_vendor=	"Samsung   ";
					break;
				case 0x90:
					dev_list[i]->device_vendor=	"Hynix     ";
					break;
				case 0x45:
					dev_list[i]->device_vendor=	"Sandisk   ";
					break;
				case 0x70:
					dev_list[i]->device_vendor=	"Kingston  ";
					break;
				default:
					dev_list[i]->device_vendor=	DEVINFO_NULL;
					break;
			}
	
		#ifdef LCT_DEVINFO_DEBUG_EMCP
    	lct_pr_debug("device vendor:%s!\n",dev_list[i]->device_vendor);
		#endif
		//device ic
		//same as module
		dev_list[i]->device_ic = dev_list[i]->device_module;

		#ifdef LCT_DEVINFO_DEBUG_EMCP
    	lct_pr_debug("device ic:%s!\n",dev_list[i]->device_ic);
		#endif
		//device version
		//NULL

		//device RAM size ,we can not get ROM size here
		//ram_size=(unsigned long)((	(unsigned int)emi_settings[i].DRAM_RANK_SIZE[0]/1024	+ (unsigned int)emi_settings[i].DRAM_RANK_SIZE[1]/1024	+ (unsigned int)emi_settings[i].DRAM_RANK_SIZE[2]/1024	+ (unsigned int)emi_settings[i].DRAM_RANK_SIZE[3]/1024)/(1024)); 
		ram_size = lct_emmc_info_str[i].ram_size;
		#ifdef LCT_DEVINFO_DEBUG_EMCP
    	//lct_pr_debug("Device info:<%x> <%x><%x><%x><%d><%ld>\n",emi_settings[i].DRAM_RANK_SIZE[0],emi_settings[i].DRAM_RANK_SIZE[1],emi_settings[i].DRAM_RANK_SIZE[2],emi_settings[i].DRAM_RANK_SIZE[3],rom_size,ram_size);
		#endif

		
				dev_list[i]->device_info = (char *)kmalloc(sizeof(dev_list[i]->device_info),GFP_KERNEL);
			//Check if used on this board// add by zhaofei - 2017-01-05-15-26
//			if((emi_settings[i].ID[0]== host->mmc->card->cid.manfid) && (emi_settings[i].ID[1]==(host->mmc->card->raw_cid[0]&0x00FF0000)>>16) && (emi_settings[i].ID[2]==(host->mmc->card->raw_cid[0]&0x0000FF00)>>8) && (emi_settings[i].ID[3]==(host->mmc->card->raw_cid[0]&0x000000FF)>>0)
//					//shaohui add the code to enhance ability of detecting more devices,for same seriers products
//				&&	(emi_settings[i].ID[4]==(host->mmc->card->raw_cid[1]&0xFF000000)>>24) && (emi_settings[i].ID[5]==(host->mmc->card->raw_cid[1]&0x00FF0000)>>16) && (emi_settings[i].ID[6]==(host->mmc->card->raw_cid[1]&0x0000FF00)>>8)
//					)
			if(0==memcmp(lct_emmc_info_str[i].cid,cid_str,15))
			{
			//sprintf(info,"ram:%dMB",ram_size);
				switch(ram_size)
				{
					case 2048:
						sprintf(dev_list[i]->device_info,"ram:2048MB+rom:%dMB",rom_size);
						break;
					case 1536:
						sprintf(dev_list[i]->device_info,"ram:1536MB+rom:%dMB",rom_size);
						break;
					case 1024:
						sprintf(dev_list[i]->device_info,"ram:1024MB+rom:%dMB",rom_size);
						break;
					case 768:
						sprintf(dev_list[i]->device_info,"ram:768 MB+rom:%dMB",rom_size);
						break;
					case 512:
						sprintf(dev_list[i]->device_info,"ram:512 MB+rom:%dMB",rom_size);
						break;
					default:
						sprintf(dev_list[i]->device_info,"ram:512 MB+rom:%dMB",rom_size);
						break;
				}


			#ifdef LCT_DEVINFO_DEBUG_EMCP
    		lct_pr_debug("Get right device!\n");
    		lct_pr_debug("Device info:%s \n",dev_list[i]->device_info);
			#endif
 		//void devinfo_declare(char* type,char* module,char* vendor,char* ic,char* version,char* info,int used)
			DEVINFO_DECLARE(dev_list[i]->device_type,dev_list[i]->device_module,dev_list[i]->device_vendor,dev_list[i]->device_ic,dev_list[i]->device_version,dev_list[i]->device_info,DEVINFO_USED );	//used device regist
			inforam_size=ram_size;
		}else{
			switch(ram_size)
			{
				case 2048:
					info1="ram:2048MB+rom:null";
					break;
				case 1536:
					info1="ram:1536MB+rom:null";
					break;
				case 1024:
					info1="ram:1024MB+rom:null";
					break;
				case 768:
					info1="ram:768 MB+rom:null";
					break;
				case 512:
					info1="ram:512 MB+rom:null";
					break;
				default:
					info1="ram:512 MB+rom:null";
					break;
			}
			#ifdef LCT_DEVINFO_DEBUG_EMCP
    		lct_pr_debug("Get wrong device!\n");
    		lct_pr_debug("Device info1:%s \n",info1);
			#endif
	//sprintf(info,"ram:%dMB",ram_size);
		DEVINFO_DECLARE(dev_list[i]->device_type,dev_list[i]->device_module,dev_list[i]->device_vendor,dev_list[i]->device_ic,dev_list[i]->device_version,info1,DEVINFO_UNUSED );	//unused device regist
		}
		}
		   

	return 0;
}


static u64 cust_msdc_get_user_capacity(struct msdc_host *host)
{
    u64 device_capacity = 0;
    u32 device_legacy_capacity = 0;
    struct mmc_host* mmc = NULL;

	mmc = host->mmc;
    if(mmc_card_mmc(mmc->card)){
        if(mmc->card->csd.read_blkbits){
            device_legacy_capacity = mmc->card->csd.capacity * (2 << (mmc->card->csd.read_blkbits - 1));
        } else{
            device_legacy_capacity = mmc->card->csd.capacity;
        }
        device_capacity = (u64)(mmc->card->ext_csd.sectors)* 512 > device_legacy_capacity ? (u64)(mmc->card->ext_csd.sectors)* 512 : device_legacy_capacity;
    }
    else if(mmc_card_sd(mmc->card)){
        device_capacity = (u64)(mmc->card->csd.capacity) << (mmc->card->csd.read_blkbits);
    }
    return device_capacity;
}

///////////////////////////shaohui end/////////////////////////////



/*shaohui add for ramproc interface start*/


static int raminfo_stats_show(struct seq_file *m, void *unused)
{

   	seq_printf(m,"Total physical ram: %ld MB\n",inforam_size);
    return 0;
}
static int raminfo_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, raminfo_stats_show, NULL);
}


static const struct file_operations raminfo_stats_fops = {
    .owner = THIS_MODULE,
    .open = raminfo_stats_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

/*shaohui add for raminfo proc interface end*/




static void devinfo_flash_regist(struct work_struct *data)
{

    struct msdc_host *host_ctl;
	//int num=0;
	//msleep(5000);		//wait for MSDC init done ,Do not use in init,this will block kernel
	//msdc_check_init_done();

#ifdef DEVINFO_DIS_DDR_STATUS
	get_boot_ram_listnum();
#endif
	//host_ctl = mtk_msdc_host[0];  //define 0 to on board emcp
	host_ctl = msdc_get_host(MSDC_EMMC, MSDC_BOOT_EN, 0);

   	rom_size = cust_msdc_get_user_capacity(mtk_msdc_host[0])/1024/1024;
	devinfo_register_emcp(host_ctl);


// add for RAM proc 
    proc_create("raminfo", S_IRUGO, NULL, &raminfo_stats_fops);


#ifdef LCT_DEVINFO_DEBUG_EMCP
	lct_pr_debug("OK now to get emmc info\n");
    lct_pr_debug("csd:0x%x,0x%x,0x%x,0x%x\n",host_ctl->mmc->card->raw_cid[0],
            host_ctl->mmc->card->raw_cid[1],
            host_ctl->mmc->card->raw_cid[2],
            host_ctl->mmc->card->raw_cid[3]);
#endif

}


static struct work_struct devinfo_msdc_check;
static int devinfo_msdc_check_init_done(void)
{
    struct msdc_host *host=NULL;
    //host = mtk_msdc_host[0];  //define 0 to on board emcp
	host = msdc_get_host(MSDC_EMMC, MSDC_BOOT_EN, 0);

	if(host==NULL || host->mmc ==NULL || host->mmc->card ==NULL)
		return -1;
	else
		return 0;

}

static void devinfo_flash_localinit(void)
{

	
	lct_pr_info("start to %s\n",__func__);
	INIT_WORK(&devinfo_msdc_check, devinfo_flash_regist);
	
	for(;;)
	{	
		if(devinfo_msdc_check_init_done() == 0){
			schedule_work(&devinfo_msdc_check);
			break;
		}
		msleep(3000);
	}
}

static int __init devinfo_flash_init(void)
{

	devinfo_flash_localinit();
    return 0;
}

static void __exit devinfo_flash_exit(void)
{

}

//module_init(devinfo_flash_init);
late_initcall_sync(devinfo_flash_init);
module_exit(devinfo_flash_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SLT DEVINFO flash memory info Management Driver");
MODULE_AUTHOR("shaohui <shaohui@longcheer.net>");
