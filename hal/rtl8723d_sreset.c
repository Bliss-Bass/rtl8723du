// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2017 Realtek Corporation */

#define _RTL8723D_SRESET_C_

#include <rtl8723d_hal.h>


#ifdef DBG_CONFIG_ERROR_DETECT
void rtl8723d_sreset_xmit_status_check(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	systime current_time;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	unsigned int diff_time;
	u32 txdma_status;

	txdma_status = rtw_read32(padapter, REG_TXDMA_STATUS);
	if (txdma_status != 0x00 && txdma_status != 0xeaeaeaea) {
		RTW_INFO("%s REG_TXDMA_STATUS:0x%08x\n", __FUNCTION__, txdma_status);
		rtw_hal_sreset_reset(padapter);
	}

	/* total xmit irp = 4 */
	/* DBG_8192C("==>%s free_xmitbuf_cnt(%d),txirp_cnt(%d)\n",__FUNCTION__,pxmitpriv->free_xmitbuf_cnt,pxmitpriv->txirp_cnt); */
	/* if(pxmitpriv->txirp_cnt == NR_XMITBUFF+1) */
	current_time = rtw_get_current_time();

	if (0 == pxmitpriv->free_xmitbuf_cnt || 0 == pxmitpriv->free_xmit_extbuf_cnt) {

		diff_time = rtw_get_passing_time_ms(psrtpriv->last_tx_time);

		if (diff_time > 2000) {
			if (psrtpriv->last_tx_complete_time == 0)
				psrtpriv->last_tx_complete_time = current_time;
			else {
				diff_time = rtw_get_passing_time_ms(psrtpriv->last_tx_complete_time);
				if (diff_time > 4000) {
					u32 ability = 0;

					/* padapter->Wifi_Error_Status = WIFI_TX_HANG; */
					ability = rtw_phydm_ability_get(padapter);

					RTW_INFO("%s tx hang %s\n", __FUNCTION__,
						(ability & ODM_BB_ADAPTIVITY) ? "ODM_BB_ADAPTIVITY" : "");

					if (!(ability & ODM_BB_ADAPTIVITY))
						rtw_hal_sreset_reset(padapter);
				}
			}
		}
	}

	if (psrtpriv->dbg_trigger_point == SRESET_TGP_XMIT_STATUS) {
		psrtpriv->dbg_trigger_point = SRESET_TGP_NULL;
		rtw_hal_sreset_reset(padapter);
		return;
	}
}

void rtl8723d_sreset_linked_status_check(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
#if 0
	u32 regc50, regc58, reg824, reg800;

	regc50 = rtw_read32(padapter, 0xc50);
	regc58 = rtw_read32(padapter, 0xc58);
	reg824 = rtw_read32(padapter, 0x824);
	reg800 = rtw_read32(padapter, 0x800);
	if (((regc50 & 0xFFFFFF00) != 0x69543400) ||
	    ((regc58 & 0xFFFFFF00) != 0x69543400) ||
	    (((reg824 & 0xFFFFFF00) != 0x00390000) && (((reg824 & 0xFFFFFF00) != 0x80390000))) ||
	    (((reg800 & 0xFFFFFF00) != 0x03040000) && ((reg800 & 0xFFFFFF00) != 0x83040000))) {
		DBG_8192C("%s regc50:0x%08x, regc58:0x%08x, reg824:0x%08x, reg800:0x%08x,\n", __FUNCTION__,
			  regc50, regc58, reg824, reg800);
		rtw_hal_sreset_reset(padapter);
	}
#endif

	if (psrtpriv->dbg_trigger_point == SRESET_TGP_LINK_STATUS) {
		psrtpriv->dbg_trigger_point = SRESET_TGP_NULL;
		rtw_hal_sreset_reset(padapter);
		return;
	}
}

#endif