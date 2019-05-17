// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2017 Realtek Corporation */

#define _RTW_TDLS_C_

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_TDLS
#define ONE_SEC 	1000 /* 1000 ms */

extern unsigned char MCS_rate_2R[16];
extern unsigned char MCS_rate_1R[16];

inline void rtw_tdls_set_link_established(_adapter *adapter, bool en)
{
	adapter->tdlsinfo.link_established = en;
	rtw_mi_update_iface_status(&(adapter->mlmepriv), 0);
}

void rtw_reset_tdls_info(_adapter *padapter)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	ptdlsinfo->ap_prohibited = _FALSE;

	/* For TDLS channel switch, currently we only allow it to work in wifi logo test mode */
	if (padapter->registrypriv.wifi_spec == 1)
		ptdlsinfo->ch_switch_prohibited = _FALSE;
	else
		ptdlsinfo->ch_switch_prohibited = _TRUE;

	rtw_tdls_set_link_established(padapter, _FALSE);
	ptdlsinfo->sta_cnt = 0;
	ptdlsinfo->sta_maximum = _FALSE;

#ifdef CONFIG_TDLS_CH_SW
	ptdlsinfo->chsw_info.ch_sw_state = TDLS_STATE_NONE;
	ATOMIC_SET(&ptdlsinfo->chsw_info.chsw_on, _FALSE);
	ptdlsinfo->chsw_info.off_ch_num = 0;
	ptdlsinfo->chsw_info.ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	ptdlsinfo->chsw_info.cur_time = 0;
	ptdlsinfo->chsw_info.delay_switch_back = _FALSE;
	ptdlsinfo->chsw_info.dump_stack = _FALSE;
#endif

	ptdlsinfo->ch_sensing = 0;
	ptdlsinfo->watchdog_count = 0;
	ptdlsinfo->dev_discovered = _FALSE;

#ifdef CONFIG_WFD
	ptdlsinfo->wfd_info = &padapter->wfd_info;
#endif

	ptdlsinfo->tdls_sctx = NULL;
}

int rtw_init_tdls_info(_adapter *padapter)
{
	int	res = _SUCCESS;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	rtw_reset_tdls_info(padapter);

#ifdef CONFIG_TDLS_DRIVER_SETUP
	ptdlsinfo->driver_setup = _TRUE;
#else
	ptdlsinfo->driver_setup = _FALSE;
#endif /* CONFIG_TDLS_DRIVER_SETUP */

	_rtw_spinlock_init(&ptdlsinfo->cmd_lock);
	_rtw_spinlock_init(&ptdlsinfo->hdl_lock);

	return res;

}

void rtw_free_tdls_info(struct tdls_info *ptdlsinfo)
{
	_rtw_spinlock_free(&ptdlsinfo->cmd_lock);
	_rtw_spinlock_free(&ptdlsinfo->hdl_lock);

	_rtw_memset(ptdlsinfo, 0, sizeof(struct tdls_info));

}

void rtw_free_all_tdls_sta(_adapter *padapter, u8 enqueue_cmd)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	_irqL	 irqL;
	_list	*plist, *phead;
	s32	index;
	struct sta_info *psta = NULL;
	struct sta_info *ptdls_sta[NUM_STA];
	u8 empty_hwaddr[ETH_ALEN] = { 0x00 };

	_rtw_memset(ptdls_sta, 0x00, sizeof(ptdls_sta));

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	for (index = 0; index < NUM_STA; index++) {
		phead = &(pstapriv->sta_hash[index]);
		plist = get_next(phead);

		while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

			plist = get_next(plist);

			if (psta->tdls_sta_state != TDLS_STATE_NONE)
				ptdls_sta[index] = psta;
		}
	}
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	for (index = 0; index < NUM_STA; index++) {
		if (ptdls_sta[index]) {
			struct TDLSoption_param tdls_param;

			psta = ptdls_sta[index];

			RTW_INFO("Do tear down to "MAC_FMT" by enqueue_cmd = %d\n", MAC_ARG(psta->cmn.mac_addr), enqueue_cmd);

			_rtw_memcpy(&(tdls_param.addr), psta->cmn.mac_addr, ETH_ALEN);
			tdls_param.option = TDLS_TEARDOWN_STA_NO_WAIT;
			tdls_hdl(padapter, (unsigned char *)&(tdls_param));

			rtw_tdls_teardown_pre_hdl(padapter, psta);

			if (enqueue_cmd == _TRUE)
				rtw_tdls_cmd(padapter, psta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
			else
			 {
				tdls_param.option = TDLS_TEARDOWN_STA_LOCALLY_POST;
				tdls_hdl(padapter, (unsigned char *)&(tdls_param));
			}
		}
	}
}

int check_ap_tdls_prohibited(u8 *pframe, u8 pkt_len)
{
	u8 tdls_prohibited_bit = 0x40; /* bit(38); TDLS_prohibited */

	if (pkt_len < 5)
		return _FALSE;

	pframe += 4;
	if ((*pframe) & tdls_prohibited_bit)
		return _TRUE;

	return _FALSE;
}

int check_ap_tdls_ch_switching_prohibited(u8 *pframe, u8 pkt_len)
{
	u8 tdls_ch_swithcing_prohibited_bit = 0x80; /* bit(39); TDLS_channel_switching prohibited */

	if (pkt_len < 5)
		return _FALSE;

	pframe += 4;
	if ((*pframe) & tdls_ch_swithcing_prohibited_bit)
		return _TRUE;

	return _FALSE;
}

u8 rtw_is_tdls_enabled(_adapter *padapter)
{
	return padapter->registrypriv.en_tdls;
}

void rtw_set_tdls_enable(_adapter *padapter, u8 enable)
{
	padapter->registrypriv.en_tdls = enable;
	RTW_INFO("%s: en_tdls = %d\n", __func__, rtw_is_tdls_enabled(padapter));
}

void rtw_enable_tdls_func(_adapter *padapter)
{
	if (rtw_is_tdls_enabled(padapter) == _TRUE)
		return;

#if 0
#ifdef CONFIG_MCC_MODE
	if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC) == _TRUE) {
		RTW_INFO("[TDLS] MCC is running, can't enable TDLS !\n");
		return;
	}
#endif
#endif
	rtw_set_tdls_enable(padapter, _TRUE);
}

void rtw_disable_tdls_func(_adapter *padapter, u8 enqueue_cmd)
{
	if (rtw_is_tdls_enabled(padapter) == _FALSE)
		return;

	rtw_free_all_tdls_sta(padapter, enqueue_cmd);
	rtw_tdls_cmd(padapter, NULL, TDLS_RS_RCR);
	rtw_reset_tdls_info(padapter);

	rtw_set_tdls_enable(padapter, _FALSE);
}

u8 rtw_is_tdls_sta_existed(_adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	int i = 0;
	_irqL irqL;
	_list	*plist, *phead;
	u8 ret = _FALSE;

	if (rtw_is_tdls_enabled(padapter) == _FALSE)
		return _FALSE;

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	for (i = 0; i < NUM_STA; i++) {
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
			plist = get_next(plist);
			if (psta->tdls_sta_state != TDLS_STATE_NONE) {
				ret = _TRUE;
				goto Exit;
			}
		}
	}

Exit:

	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	return ret;
}

u8 rtw_tdls_is_setup_allowed(_adapter *padapter)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	if (is_client_associated_to_ap(padapter) == _FALSE)
		return _FALSE;

	if (ptdlsinfo->ap_prohibited == _TRUE)
		return _FALSE;

	return _TRUE;
}

#ifdef CONFIG_TDLS_CH_SW
u8 rtw_tdls_is_chsw_allowed(_adapter *padapter)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	if (ptdlsinfo->ch_switch_prohibited == _TRUE)
		return _FALSE;

	if (padapter->registrypriv.wifi_spec == 0)
		return _FALSE;

	return _TRUE;
}
#endif

int _issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, unsigned char *da, unsigned int power_mode, int wait_ms)
{
	int ret = _FAIL;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl, *qc;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen += 2;
	pattrib->qos_en = _TRUE;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;
	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if (power_mode)
		SetPwrMgt(fctrl);

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);

	SetPriority(qc, 7);	/* Set priority to VO */

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (wait_ms)
		ret = dump_mgntframe_and_wait_ack_timeout(padapter, pmgntframe, wait_ms);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _SUCCESS;
	}

exit:
	return ret;

}

/*
 *wait_ms == 0 means that there is no need to wait ack through C2H_CCX_TX_RPT
 *wait_ms > 0 means you want to wait ack through C2H_CCX_TX_RPT, and the value of wait_ms means the interval between each TX
 *try_cnt means the maximal TX count to try
 */
int issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms)
{
	int ret;
	int i = 0;
	systime start = rtw_get_current_time();
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

#if 0
	psta = rtw_get_stainfo(&padapter->stapriv, da);
	if (psta) {
		if (power_mode)
			rtw_hal_macid_sleep(padapter, psta->cmn.mac_id);
		else
			rtw_hal_macid_wakeup(padapter, psta->cmn.mac_id);
	} else {
		RTW_INFO(FUNC_ADPT_FMT ": Can't find sta info for " MAC_FMT ", skip macid %s!!\n",
			FUNC_ADPT_ARG(padapter), MAC_ARG(da), power_mode ? "sleep" : "wakeup");
		rtw_warn_on(1);
	}
#endif

	do {
		ret = _issue_nulldata_to_TDLS_peer_STA(padapter, da, power_mode, wait_ms);

		i++;

		if (RTW_CANNOT_RUN(padapter))
			break;

		if (i < try_cnt && wait_ms > 0 && ret == _FAIL)
			rtw_msleep_os(wait_ms);

	} while ((i < try_cnt) && (ret == _FAIL || wait_ms == 0));

	if (ret != _FAIL) {
		ret = _SUCCESS;
#ifndef DBG_XMIT_ACK
		goto exit;
#endif
	}

	if (try_cnt && wait_ms) {
		if (da)
			RTW_INFO(FUNC_ADPT_FMT" to "MAC_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), MAC_ARG(da), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
		else
			RTW_INFO(FUNC_ADPT_FMT", ch:%u%s, %d/%d in %u ms\n",
				FUNC_ADPT_ARG(padapter), rtw_get_oper_ch(padapter),
				ret == _SUCCESS ? ", acked" : "", i, try_cnt, rtw_get_passing_time_ms(start));
	}
exit:
	return ret;
}

/* TDLS encryption(if needed) will always be CCMP */
void rtw_tdls_set_key(_adapter *padapter, struct sta_info *ptdls_sta)
{
	ptdls_sta->dot118021XPrivacy = _AES_;
	rtw_setstakey_cmd(padapter, ptdls_sta, TDLS_KEY, _TRUE);
}

void rtw_tdls_process_ht_cap(_adapter *padapter, struct sta_info *ptdls_sta, u8 *data, u8 Length)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	u8	max_AMPDU_len, min_MPDU_spacing;
	u8	cur_ldpc_cap = 0, cur_stbc_cap = 0, cur_beamform_cap = 0;

	/* Save HT capabilities in the sta object */
	_rtw_memset(&ptdls_sta->htpriv.ht_cap, 0, sizeof(struct rtw_ieee80211_ht_cap));
	if (data && Length >= sizeof(struct rtw_ieee80211_ht_cap)) {
		ptdls_sta->flags |= WLAN_STA_HT;
		ptdls_sta->flags |= WLAN_STA_WME;

		_rtw_memcpy(&ptdls_sta->htpriv.ht_cap, data, sizeof(struct rtw_ieee80211_ht_cap));
	} else {
		ptdls_sta->flags &= ~WLAN_STA_HT;
		return;
	}

	if (ptdls_sta->flags & WLAN_STA_HT) {
		if (padapter->registrypriv.ht_enable == _TRUE) {
			ptdls_sta->htpriv.ht_option = _TRUE;
			ptdls_sta->qos_option = _TRUE;
		} else {
			ptdls_sta->htpriv.ht_option = _FALSE;
			ptdls_sta->qos_option = _FALSE;
		}
	}

	/* HT related cap */
	if (ptdls_sta->htpriv.ht_option) {
		/* Check if sta supports rx ampdu */
		if (padapter->registrypriv.ampdu_enable == 1)
			ptdls_sta->htpriv.ampdu_enable = _TRUE;

		/* AMPDU Parameters field */
		/* Get MIN of MAX AMPDU Length Exp */
		if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3) > (data[2] & 0x3))
			max_AMPDU_len = (data[2] & 0x3);
		else
			max_AMPDU_len = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3);
		/* Get MAX of MIN MPDU Start Spacing */
		if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) > (data[2] & 0x1c))
			min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c);
		else
			min_MPDU_spacing = (data[2] & 0x1c);
		ptdls_sta->htpriv.rx_ampdu_min_spacing = max_AMPDU_len | min_MPDU_spacing;

		/* Check if sta support s Short GI 20M */
		if ((phtpriv->sgi_20m == _TRUE) && (ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SGI_20)))
			ptdls_sta->htpriv.sgi_20m = _TRUE;

		/* Check if sta support s Short GI 40M */
		if ((phtpriv->sgi_40m == _TRUE) && (ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SGI_40)))
			ptdls_sta->htpriv.sgi_40m = _TRUE;

		/* Bwmode would still followed AP's setting */
		if (ptdls_sta->htpriv.ht_cap.cap_info & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH)) {
			if (padapter->mlmeextpriv.cur_bwmode >= CHANNEL_WIDTH_40)
				ptdls_sta->cmn.bw_mode = CHANNEL_WIDTH_40;
			ptdls_sta->htpriv.ch_offset = padapter->mlmeextpriv.cur_ch_offset;
		}

		/* Config LDPC Coding Capability */
		if (TEST_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_TX) && GET_HT_CAP_ELE_LDPC_CAP(data)) {
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx LDPC!\n");
		}
		ptdls_sta->htpriv.ldpc_cap = cur_ldpc_cap;

		/* Config STBC setting */
		if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX) && GET_HT_CAP_ELE_RX_STBC(data)) {
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx STBC!\n");
		}
		ptdls_sta->htpriv.stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
		/* Config Tx beamforming setting */
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(data))
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(data))
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
		ptdls_sta->htpriv.beamform_cap = cur_beamform_cap;
		if (cur_beamform_cap)
			RTW_INFO("Client HT Beamforming Cap = 0x%02X\n", cur_beamform_cap);
#endif /* CONFIG_BEAMFORMING */
	}

}

u8 *rtw_tdls_set_ht_cap(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	rtw_ht_use_default_setting(padapter);

	if (padapter->registrypriv.wifi_spec == 1) {
		padapter->mlmepriv.htpriv.sgi_20m = _FALSE;
		padapter->mlmepriv.htpriv.sgi_40m = _FALSE;
	}

	rtw_restructure_ht_ie(padapter, NULL, pframe, 0, &(pattrib->pktlen), padapter->mlmeextpriv.cur_channel);

	return pframe + pattrib->pktlen;
}

u8 *rtw_tdls_set_sup_ch(_adapter *adapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u8 sup_ch[30 * 2] = {0x00}, ch_set_idx = 0, sup_ch_idx = 2;

	while (ch_set_idx < rfctl->max_chan_nums && rfctl->channel_set[ch_set_idx].channelnum != 0) {
		if (rfctl->channel_set[ch_set_idx].channelnum <= 14) {
			/* todo: fix 2.4g supported channel when channel doesn't start from 1 and continuous */
			sup_ch[0] = 1;	/* first channel number */
			sup_ch[1] = rfctl->channel_set[ch_set_idx].channelnum;	/* number of channel */
		} else {
			sup_ch[sup_ch_idx++] = rfctl->channel_set[ch_set_idx].channelnum;
			sup_ch[sup_ch_idx++] = 1;
		}
		ch_set_idx++;
	}

	return rtw_set_ie(pframe, _supported_ch_ie_, sup_ch_idx, sup_ch, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_rsnie(struct tdls_txmgmt *ptxmgmt, u8 *pframe, struct pkt_attrib *pattrib,  int init, struct sta_info *ptdls_sta)
{
	u8 *p = null;
	int len = 0;

	if (ptxmgmt->len > 0)
		p = rtw_get_ie(ptxmgmt->buf, _rsn_ie_2_, &len, ptxmgmt->len);

	if (p != null)
		return rtw_set_ie(pframe, _rsn_ie_2_, len, p + 2, &(pattrib->pktlen));
	else if (init == _true)
		return rtw_set_ie(pframe, _rsn_ie_2_, sizeof(tdls_rsnie), tdls_rsnie, &(pattrib->pktlen));
	else
		return rtw_set_ie(pframe, _rsn_ie_2_, sizeof(ptdls_sta->tdls_rsnie), ptdls_sta->tdls_rsnie, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_ext_cap(u8 *pframe, struct pkt_attrib *pattrib)
{
	return rtw_set_ie(pframe, _ext_cap_ie_ , sizeof(tdls_ext_capie), tdls_ext_capie, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_qos_cap(u8 *pframe, struct pkt_attrib *pattrib)
{
	return rtw_set_ie(pframe, _vendor_specific_ie_, sizeof(tdls_wmmie), tdls_wmmie,  &(pattrib->pktlen));
}

u8 *rtw_tdls_set_ftie(struct tdls_txmgmt *ptxmgmt, u8 *pframe, struct pkt_attrib *pattrib, u8 *anonce, u8 *snonce)
{
	struct wpa_tdls_ftie ftie = {0};
	u8 *p = null;
	int len = 0;

	if (ptxmgmt->len > 0)
		p = rtw_get_ie(ptxmgmt->buf, _ftie_, &len, ptxmgmt->len);

	if (p != null)
		return rtw_set_ie(pframe, _ftie_, len, p + 2, &(pattrib->pktlen));
	else {
		if (anonce != null)
			_rtw_memcpy(ftie.anonce, anonce, wpa_nonce_len);
		if (snonce != null)
			_rtw_memcpy(ftie.snonce, snonce, wpa_nonce_len);

		return rtw_set_ie(pframe, _ftie_, tdls_ftie_data_len,
						  (u8 *)ftie.data, &(pattrib->pktlen));
	}
}

u8 *rtw_tdls_set_timeout_interval(struct tdls_txmgmt *ptxmgmt, u8 *pframe, struct pkt_attrib *pattrib, int init, struct sta_info *ptdls_sta)
{
	u8 timeout_itvl[5];	/* set timeout interval to maximum value */
	u32 timeout_interval = tdls_tpk_resend_count;
	u8 *p = null;
	int len = 0;

	if (ptxmgmt->len > 0)
		p = rtw_get_ie(ptxmgmt->buf, _timeout_itvl_ie_, &len, ptxmgmt->len);

	if (p != null)
		return rtw_set_ie(pframe, _timeout_itvl_ie_, len, p + 2, &(pattrib->pktlen));
	else {
		/* timeout interval */
		timeout_itvl[0] = 0x02;
		if (init == _true)
			_rtw_memcpy(timeout_itvl + 1, &timeout_interval, 4);
		else
			_rtw_memcpy(timeout_itvl + 1, (u8 *)(&ptdls_sta->tdls_peerkey_lifetime), 4);

		return rtw_set_ie(pframe, _timeout_itvl_ie_, 5, timeout_itvl, &(pattrib->pktlen));
	}
}

u8 *rtw_tdls_set_bss_coexist(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	u8 iedata = 0;

	if (padapter->mlmepriv.num_fortymhzintolerant > 0)
		iedata |= bit(2);	/* 20 mhz bss width request */

	/* information bit should be set by tdls test plan 5.9 */
	iedata |= bit(0);
	return rtw_set_ie(pframe, eid_bsscoexistence, 1, &iedata, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_payload_type(u8 *pframe, struct pkt_attrib *pattrib)
{
	u8 payload_type = 0x02;
	return rtw_set_fixed_ie(pframe, 1, &(payload_type), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_category(u8 *pframe, struct pkt_attrib *pattrib, u8 category)
{
	return rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_action(u8 *pframe, struct pkt_attrib *pattrib, struct tdls_txmgmt *ptxmgmt)
{
	return rtw_set_fixed_ie(pframe, 1, &(ptxmgmt->action_code), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_status_code(u8 *pframe, struct pkt_attrib *pattrib, struct tdls_txmgmt *ptxmgmt)
{
	return rtw_set_fixed_ie(pframe, 2, (u8 *)&(ptxmgmt->status_code), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_dialog(u8 *pframe, struct pkt_attrib *pattrib, struct tdls_txmgmt *ptxmgmt)
{
	u8 dialogtoken = 1;
	if (ptxmgmt->dialog_token)
		return rtw_set_fixed_ie(pframe, 1, &(ptxmgmt->dialog_token), &(pattrib->pktlen));
	else
		return rtw_set_fixed_ie(pframe, 1, &(dialogtoken), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_reg_class(u8 *pframe, struct pkt_attrib *pattrib, struct sta_info *ptdls_sta)
{
	u8 reg_class = 22;
	return rtw_set_fixed_ie(pframe, 1, &(reg_class), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_second_channel_offset(u8 *pframe, struct pkt_attrib *pattrib, u8 ch_offset)
{
	return rtw_set_ie(pframe, eid_secondarychnloffset , 1, &ch_offset, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_capability(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	u8 cap_from_ie[2] = {0};

	_rtw_memcpy(cap_from_ie, rtw_get_capability_from_ie(pmlmeinfo->network.ies), 2);

	return rtw_set_fixed_ie(pframe, 2, cap_from_ie, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_supported_rate(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	u8 bssrate[ndis_802_11_length_rates_ex];
	int bssrate_len = 0;
	u8 more_supportedrates = 0;

	rtw_set_supported_rate(bssrate, (padapter->registrypriv.wireless_mode == wireless_mode_max) ? padapter->mlmeextpriv.cur_wireless_mode : padapter->registrypriv.wireless_mode);
	bssrate_len = rtw_get_rateset_len(bssrate);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _supportedrates_ie_ , 8, bssrate, &(pattrib->pktlen));
		more_supportedrates = 1;
	} else
		pframe = rtw_set_ie(pframe, _supportedrates_ie_ , bssrate_len , bssrate, &(pattrib->pktlen));

	/* extended supported rates */
	if (more_supportedrates == 1)
		pframe = rtw_set_ie(pframe, _ext_supportedrates_ie_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));

	return pframe;
}

u8 *rtw_tdls_set_sup_reg_class(u8 *pframe, struct pkt_attrib *pattrib)
{
	return rtw_set_ie(pframe, _src_ie_ , sizeof(tdls_src), tdls_src, &(pattrib->pktlen));
}

u8 *rtw_tdls_set_linkid(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib, u8 init)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	u8 link_id_addr[18] = {0};

	_rtw_memcpy(link_id_addr, get_my_bssid(&(pmlmeinfo->network)), 6);

	if (init == _true) {
		_rtw_memcpy((link_id_addr + 6), pattrib->src, 6);
		_rtw_memcpy((link_id_addr + 12), pattrib->dst, 6);
	} else {
		_rtw_memcpy((link_id_addr + 6), pattrib->dst, 6);
		_rtw_memcpy((link_id_addr + 12), pattrib->src, 6);
	}
	return rtw_set_ie(pframe, _link_id_ie_, 18, link_id_addr, &(pattrib->pktlen));
}

#ifdef config_tdls_ch_sw
u8 *rtw_tdls_set_target_ch(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	u8 target_ch = 1;
	if (padapter->tdlsinfo.chsw_info.off_ch_num)
		return rtw_set_fixed_ie(pframe, 1, &(padapter->tdlsinfo.chsw_info.off_ch_num), &(pattrib->pktlen));
	else
		return rtw_set_fixed_ie(pframe, 1, &(target_ch), &(pattrib->pktlen));
}

u8 *rtw_tdls_set_ch_sw(u8 *pframe, struct pkt_attrib *pattrib, struct sta_info *ptdls_sta)
{
	u8 ch_switch_timing[4] = {0};
	u16 switch_time = (ptdls_sta->ch_switch_time >= tdls_ch_switch_time * 1000) ?
			  ptdls_sta->ch_switch_time : tdls_ch_switch_time;
	u16 switch_timeout = (ptdls_sta->ch_switch_timeout >= tdls_ch_switch_timeout * 1000) ?
		     ptdls_sta->ch_switch_timeout : tdls_ch_switch_timeout;

	_rtw_memcpy(ch_switch_timing, &switch_time, 2);
	_rtw_memcpy(ch_switch_timing + 2, &switch_timeout, 2);

	return rtw_set_ie(pframe, _ch_switch_timing_,  4, ch_switch_timing, &(pattrib->pktlen));
}

void rtw_tdls_set_ch_sw_oper_control(_adapter *padapter, u8 enable)
{
	if (atomic_read(&padapter->tdlsinfo.chsw_info.chsw_on) != enable)
		atomic_set(&padapter->tdlsinfo.chsw_info.chsw_on, enable);

	rtw_hal_set_hwreg(padapter, hw_var_tdls_bcn_early_c2h_rpt, &enable);
	rtw_info("[tdls] %s bcn early c2h report\n", (enable == _true) ? "start" : "stop");
}

void rtw_tdls_ch_sw_back_to_base_chnl(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv;
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;

	pmlmepriv = &padapter->mlmepriv;

	if ((atomic_read(&pchsw_info->chsw_on) == _true) &&
	    (padapter->mlmeextpriv.cur_channel != rtw_get_oper_ch(padapter)))
		rtw_tdls_cmd(padapter, pchsw_info->addr, tdls_ch_sw_to_base_chnl_unsolicited);
}

static void rtw_tdls_chsw_oper_init(_adapter *padapter, u32 timeout_ms)
{
	struct submit_ctx	*chsw_sctx = &padapter->tdlsinfo.chsw_info.chsw_sctx;

	rtw_sctx_init(chsw_sctx, timeout_ms);
}

static int rtw_tdls_chsw_oper_wait(_adapter *padapter)
{
	struct submit_ctx	*chsw_sctx = &padapter->tdlsinfo.chsw_info.chsw_sctx;

	return rtw_sctx_wait(chsw_sctx, __func__);
}

void rtw_tdls_chsw_oper_done(_adapter *padapter)
{
	struct submit_ctx	*chsw_sctx = &padapter->tdlsinfo.chsw_info.chsw_sctx;

	rtw_sctx_done(&chsw_sctx);
}

s32 rtw_tdls_do_ch_sw(_adapter *padapter, struct sta_info *ptdls_sta, u8 chnl_type, u8 channel, u8 channel_offset, u16 bwmode, u16 ch_switch_time)
{
	hal_data_type *phaldata = get_hal_data(padapter);
	u8 center_ch, chnl_offset80 = hal_prime_chnl_offset_dont_care;
	u32 ch_sw_time_start, ch_sw_time_spent, wait_time;
	u8 take_care_iqk;
	s32 ret = _fail;

	ch_sw_time_start = rtw_systime_to_ms(rtw_get_current_time());

	/* set mac_id sleep before channel switch */
	rtw_hal_macid_sleep(padapter, ptdls_sta->cmn.mac_id);
	
#ifdef config_tdls_ch_sw_by_drv
	set_channel_bwmode(padapter, channel, channel_offset, bwmode);
	ret = _success;
#else
	rtw_tdls_chsw_oper_init(padapter, tdls_ch_switch_oper_offload_timeout);

	/* channel switch ios offload to fw */
	if (rtw_hal_ch_sw_oper_offload(padapter, channel, channel_offset, bwmode) == _success) {
		if (rtw_tdls_chsw_oper_wait(padapter) == _success) {
			/* set channel and bw related variables in driver */
			_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), null);

			rtw_set_oper_ch(padapter, channel);
			rtw_set_oper_choffset(padapter, channel_offset);
			rtw_set_oper_bw(padapter, bwmode);

			center_ch = rtw_get_center_ch(channel, bwmode, channel_offset);
			phaldata->current_channel = center_ch;
			phaldata->currentcenterfrequencyindex1 = center_ch;
			phaldata->current_channel_bw = bwmode;
			phaldata->ncur40mhzprimesc = channel_offset;

			if (bwmode == channel_width_80) {
				if (center_ch > channel)
					chnl_offset80 = hal_prime_chnl_offset_lower;
				else if (center_ch < channel)
					chnl_offset80 = hal_prime_chnl_offset_upper;
				else
					chnl_offset80 = hal_prime_chnl_offset_dont_care;
			}
			phaldata->ncur80mhzprimesc = chnl_offset80;

			phaldata->currentcenterfrequencyindex1 = center_ch;

			_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), null);

			rtw_hal_get_hwreg(padapter, hw_var_ch_sw_need_to_take_care_iqk_info, &take_care_iqk);
			if (take_care_iqk == _true)
				rtw_hal_ch_sw_iqk_info_restore(padapter, ch_sw_use_case_tdls);

			ret = _success;
		} else
			rtw_info("[tdls] chsw oper wait fail !!\n");
	}
#endif

	if (ret == _success) {
		ch_sw_time_spent = rtw_systime_to_ms(rtw_get_current_time()) - ch_sw_time_start;
		if (chnl_type == tdls_ch_sw_off_chnl) {
			if ((u32)ch_switch_time / 1000 > ch_sw_time_spent)
				wait_time = (u32)ch_switch_time / 1000 - ch_sw_time_spent;
			else
				wait_time = 0;

			if (wait_time > 0)
				rtw_msleep_os(wait_time);
		}
	}

	/* set mac_id wakeup after channel switch */
	rtw_hal_macid_wakeup(padapter, ptdls_sta->cmn.mac_id);

	return ret;
}
#endif

u8 *rtw_tdls_set_wmm_params(_adapter *padapter, u8 *pframe, struct pkt_attrib *pattrib)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 wmm_param_ele[24] = {0};

	if (&pmlmeinfo->wmm_param) {
		_rtw_memcpy(wmm_param_ele, wmm_para_oui, 6);
		if (_rtw_memcmp(&pmlmeinfo->wmm_param, &wmm_param_ele[6], 18) == _true)
			/* use default wmm param */
			_rtw_memcpy(wmm_param_ele + 6, (u8 *)&tdls_wmm_param_ie, sizeof(tdls_wmm_param_ie));
		else
			_rtw_memcpy(wmm_param_ele + 6, (u8 *)&pmlmeinfo->wmm_param, sizeof(pmlmeinfo->wmm_param));
		return rtw_set_ie(pframe, _vendor_specific_ie_,  24, wmm_param_ele, &(pattrib->pktlen));
	} else
		return pframe;
}

#ifdef config_wfd
void rtw_tdls_process_wfd_ie(struct tdls_info *ptdlsinfo, u8 *ptr, u8 length)
{
	u8 *wfd_ie;
	u32	wfd_ielen = 0;

	if (!hal_chk_wl_func(tdls_info_to_adapter(ptdlsinfo), wl_func_miracast))
		return;

	/* try to get the tcp port information when receiving the negotiation response. */

	wfd_ie = rtw_get_wfd_ie(ptr, length, null, &wfd_ielen);
	while (wfd_ie) {
		u8 *attr_content;
		u32	attr_contentlen = 0;
		int	i;

		rtw_info("[%s] wfd ie found!!\n", __function__);
		attr_content = rtw_get_wfd_attr_content(wfd_ie, wfd_ielen, wfd_attr_device_info, null, &attr_contentlen);
		if (attr_content && attr_contentlen) {
			ptdlsinfo->wfd_info->peer_rtsp_ctrlport = rtw_get_be16(attr_content + 2);
			rtw_info("[%s] peer port num = %d\n", __function__, ptdlsinfo->wfd_info->peer_rtsp_ctrlport);
		}

		attr_content = rtw_get_wfd_attr_content(wfd_ie, wfd_ielen, wfd_attr_local_ip_addr, null, &attr_contentlen);
		if (attr_content && attr_contentlen) {
			_rtw_memcpy(ptdlsinfo->wfd_info->peer_ip_address, (attr_content + 1), 4);
			rtw_info("[%s] peer ip = %02u.%02u.%02u.%02u\n", __function__,
				ptdlsinfo->wfd_info->peer_ip_address[0], ptdlsinfo->wfd_info->peer_ip_address[1],
				ptdlsinfo->wfd_info->peer_ip_address[2], ptdlsinfo->wfd_info->peer_ip_address[3]);
		}

		wfd_ie = rtw_get_wfd_ie(wfd_ie + wfd_ielen, (ptr + length) - (wfd_ie + wfd_ielen), null, &wfd_ielen);
	}
}

int issue_tunneled_probe_req(_adapter *padapter)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	u8 baddr[eth_alen] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct tdls_txmgmt txmgmt;
	int ret = _fail;

	rtw_info("[%s]\n", __function__);

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = tunneled_probe_req;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, baddr, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	ret = _success;
exit:

	return ret;
}

int issue_tunneled_probe_rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _fail;

	rtw_info("[%s]\n", __function__);

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = tunneled_probe_rsp;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, precv_frame->u.hdr.attrib.src, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	ret = _success;
exit:

	return ret;
}
#endif /* config_wfd */

int issue_tdls_setup_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, int wait_ack)
{
	struct tdls_info	*ptdlsinfo = &padapter->tdlsinfo;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *ptdls_sta = null;
	_irql irql;
	int ret = _fail;
	/* retry timer should be set at least 301 sec, using tpk_count counting 301 times. */
	u32 timeout_interval = tdls_tpk_resend_count;

	rtw_info("[tdls] %s\n", __function__);

	if (rtw_tdls_is_setup_allowed(padapter) == _false)
		goto exit;

	if (is_mcast(ptxmgmt->peer))
		goto exit;

	ptdls_sta = rtw_get_stainfo(pstapriv, ptxmgmt->peer);
	if (ptdlsinfo->sta_maximum == _true) {
		if (ptdls_sta == null)
			goto exit;
		else if (!(ptdls_sta->tdls_sta_state & tdls_linked_state))
			goto exit;
	}

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	if (ptdls_sta == null) {
		ptdls_sta = rtw_alloc_stainfo(pstapriv, ptxmgmt->peer);
		if (ptdls_sta == null) {
			rtw_info("[%s] rtw_alloc_stainfo fail\n", __function__);
			rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
			rtw_free_xmitframe(pxmitpriv, pmgntframe);
			goto exit;
		}
		ptdlsinfo->sta_cnt++;
	}

	ptxmgmt->action_code = tdls_setup_request;

	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);

	if (ptdlsinfo->sta_cnt == max_allowed_tdls_sta_num)
		ptdlsinfo->sta_maximum  = _true;

	ptdls_sta->tdls_sta_state |= tdls_responder_state;

	if (rtw_tdls_is_driver_setup(padapter) == _true) {
		ptdls_sta->tdls_peerkey_lifetime = timeout_interval;
		_set_timer(&ptdls_sta->handshake_timer, tdls_handshake_time);
	}

	pattrib->qsel = pattrib->priority;

	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _success;
	}

exit:

	return ret;
}

int _issue_tdls_teardown(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta, u8 wait_ack)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	_irql irql;
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	ptxmgmt->action_code = tdls_teardown;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	rtw_mi_set_scan_deny(padapter, 550);
	rtw_mi_scan_abort(padapter, _true);

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);

	if (ptxmgmt->status_code == _rson_tdls_tear_un_rsn_)
		_rtw_memcpy(pattrib->ra, ptxmgmt->peer, eth_alen);
	else
		_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);

	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	if (rtw_tdls_is_driver_setup(padapter) == _true)
		if (ptdls_sta->tdls_sta_state & tdls_linked_state)
			if (pattrib->encrypt)
				_cancel_timer_ex(&ptdls_sta->tpk_timer);

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack(padapter, pmgntframe);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _success;
	}

exit:

	return ret;
}

int issue_tdls_teardown(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 wait_ack)
{
	struct sta_info *ptdls_sta = null;
	int ret = _fail;

	ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), ptxmgmt->peer);
	if (ptdls_sta == null) {
		rtw_info("no tdls_sta for tearing down\n");
		goto exit;
	}

	ret = _issue_tdls_teardown(padapter, ptxmgmt, ptdls_sta, wait_ack);
	if ((ptxmgmt->status_code == _rson_tdls_tear_un_rsn_) && (ret == _fail)) {
		/* change status code and send teardown again via ap */
		ptxmgmt->status_code = _rson_tdls_tear_toofar_;
		ret = _issue_tdls_teardown(padapter, ptxmgmt, ptdls_sta, wait_ack);
	}

	if (rtw_tdls_is_driver_setup(padapter)) {
		rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
		rtw_tdls_cmd(padapter, ptxmgmt->peer, tdls_teardown_sta_locally_post);
	}

exit:
	return ret;
}

int issue_tdls_dis_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	ptxmgmt->action_code = tdls_discovery_request;
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}
	dump_mgntframe(padapter, pmgntframe);
	rtw_info("issue tdls dis req\n");

	ret = _success;
exit:

	return ret;
}

int issue_tdls_setup_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	ptxmgmt->action_code = tdls_setup_response;
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(&(padapter->mlmepriv)), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);

	ret = _success;
exit:

	return ret;

}

int issue_tdls_setup_cfm(_adapter *padapter, struct tdls_txmgmt *ptxmgmt)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	ptxmgmt->action_code = tdls_setup_confirm;
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;
	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(&padapter->mlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);

	ret = _success;
exit:

	return ret;

}

/* tdls discovery response frame is a management action frame */
int issue_tdls_dis_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 privacy)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, wlanhdr_offset + txdesc_offset);

	pframe = (u8 *)(pmgntframe->buf_addr) + txdesc_offset;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* unicast probe request frame */
	_rtw_memcpy(pwlanhdr->addr1, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->dst, pwlanhdr->addr1, eth_alen);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->src, pwlanhdr->addr2, eth_alen);
	_rtw_memcpy(pwlanhdr->addr3, get_bssid(&padapter->mlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ra, pwlanhdr->addr3, eth_alen);

	setseqnum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	set_frame_sub_type(pframe, wifi_action);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	rtw_build_tdls_dis_rsp_ies(padapter, pmgntframe, pframe, ptxmgmt, privacy);

	pattrib->nr_frags = 1;
	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
	ret = _success;

exit:
	return ret;
}

int issue_tdls_peer_traffic_rsp(_adapter *padapter, struct sta_info *ptdls_sta, struct tdls_txmgmt *ptxmgmt)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	ptxmgmt->action_code = tdls_peer_traffic_response;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptdls_sta->cmn.mac_addr, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, ptdls_sta->cmn.mac_addr, eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;

	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _success;

exit:

	return ret;
}

int issue_tdls_peer_traffic_indication(_adapter *padapter, struct sta_info *ptdls_sta)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = tdls_peer_traffic_indication;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptdls_sta->cmn.mac_addr, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	/* pti frame's priority should be ac_vo */
	pattrib->priority = 7;

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _success;

exit:

	return ret;
}

#ifdef config_tdls_ch_sw
int issue_tdls_ch_switch_req(_adapter *padapter, struct sta_info *ptdls_sta)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct tdls_txmgmt txmgmt;
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	if (rtw_tdls_is_chsw_allowed(padapter) == _false) {
		rtw_info("[tdls] ignore %s since channel switch is not allowed\n", __func__);
		goto exit;
	}

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	txmgmt.action_code = tdls_channel_switch_request;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptdls_sta->cmn.mac_addr, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, ptdls_sta->cmn.mac_addr, eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, &txmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	dump_mgntframe(padapter, pmgntframe);
	ret = _success;
exit:

	return ret;
}

int issue_tdls_ch_switch_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, int wait_ack)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	int ret = _fail;

	rtw_info("[tdls] %s\n", __function__);

	if (rtw_tdls_is_chsw_allowed(padapter) == _false) {
		rtw_info("[tdls] ignore %s since channel switch is not allowed\n", __func__);
		goto exit;
	}

	ptxmgmt->action_code = tdls_channel_switch_response;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == null)
		goto exit;

	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = data_frametag;
	pattrib->ether_type = 0x890d;

	_rtw_memcpy(pattrib->dst, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->src, adapter_mac_addr(padapter), eth_alen);
	_rtw_memcpy(pattrib->ra, ptxmgmt->peer, eth_alen);
	_rtw_memcpy(pattrib->ta, pattrib->src, eth_alen);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel = pattrib->priority;
	/*
		_enter_critical_bh(&pxmitpriv->lock, &irql);
		if(xmitframe_enqueue_for_tdls_sleeping_sta(padapter, pmgntframe)==_true){
			_exit_critical_bh(&pxmitpriv->lock, &irql);
			return _false;
		}
	*/
	if (rtw_xmit_tdls_coalesce(padapter, pmgntframe, ptxmgmt) != _success) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pmgntframe);
		goto exit;
	}

	if (wait_ack)
		ret = dump_mgntframe_and_wait_ack_timeout(padapter, pmgntframe, 10);
	else {
		dump_mgntframe(padapter, pmgntframe);
		ret = _success;
	}
exit:

	return ret;
}
#endif

int on_tdls_dis_rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info *ptdls_sta = null, *psta = rtw_get_stainfo(&(padapter->stapriv), get_bssid(&(padapter->mlmepriv)));
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	u8 *ptr = precv_frame->u.hdr.rx_data, *psa;
	struct rx_pkt_attrib *pattrib = &(precv_frame->u.hdr.attrib);
	struct tdls_info *ptdlsinfo = &(padapter->tdlsinfo);
	u8 empty_addr[eth_alen] = { 0x00 };
	int rssi = 0;
	struct tdls_txmgmt txmgmt;
	int ret = _success;

	if (psta)
		rssi = psta->cmn.rssi_stat.rssi;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	/* wfdtdls: for sigma test, not to setup direct link automatically */
	ptdlsinfo->dev_discovered = _true;

	psa = get_sa(ptr);
	ptdls_sta = rtw_get_stainfo(&(padapter->stapriv), psa);
	if (ptdls_sta != null)
		ptdls_sta->sta_stats.rx_tdls_disc_rsp_pkts++;

#ifdef config_tdls_autosetup
	if (ptdls_sta != null) {
		/* record the tdls sta with lowest signal strength */
		if (ptdlsinfo->sta_maximum == _true && ptdls_sta->alive_count >= 1) {
			if (_rtw_memcmp(ptdlsinfo->ss_record.macaddr, empty_addr, eth_alen)) {
				_rtw_memcpy(ptdlsinfo->ss_record.macaddr, psa, eth_alen);
				ptdlsinfo->ss_record.rxpwdball = pattrib->phy_info.rx_pwdb_all;
			} else {
				if (ptdlsinfo->ss_record.rxpwdball < pattrib->phy_info.rx_pwdb_all) {
					_rtw_memcpy(ptdlsinfo->ss_record.macaddr, psa, eth_alen);
					ptdlsinfo->ss_record.rxpwdball = pattrib->phy_info.rx_pwdb_all;
				}
			}
		}
	} else {
		if (ptdlsinfo->sta_maximum == _true) {
			if (_rtw_memcmp(ptdlsinfo->ss_record.macaddr, empty_addr, eth_alen)) {
				/* all traffics are busy, do not set up another direct link. */
				ret = _fail;
				goto exit;
			} else {
				if (pattrib->phy_info.rx_pwdb_all > ptdlsinfo->ss_record.rxpwdball) {
					_rtw_memcpy(txmgmt.peer, ptdlsinfo->ss_record.macaddr, eth_alen);
					/* issue_tdls_teardown(padapter, ptdlsinfo->ss_record.macaddr, _false); */
				} else {
					ret = _fail;
					goto exit;
				}
			}
		}


		if (pattrib->phy_info.rx_pwdb_all + tdls_signal_thresh >= rssi) {
			rtw_info("pattrib->rxpwdball=%d, pdmpriv->undecorated_smoothed_pwdb=%d\n", pattrib->phy_info.rx_pwdb_all, rssi);
			_rtw_memcpy(txmgmt.peer, psa, eth_alen);
			issue_tdls_setup_req(padapter, &txmgmt, _false);
		}
	}
#endif /* config_tdls_autosetup */

exit:
	return ret;

}

sint on_tdls_setup_req(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 *psa, *pmyid;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	_irql irql;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *prsnie, *ppairwise_cipher;
	u8 i, k;
	u8 ccmp_included = 0, rsnie_included = 0;
	u16 j, pairwise_count;
	u8 snonce[32];
	u32 timeout_interval = tdls_tpk_resend_count;
	sint parsing_length;	/* frame body length, without icv_len */
	pndis_802_11_variable_ies	pie;
	u8 fixed_ie = 5;
	unsigned char		supportrate[16];
	int				supportratenum = 0;
	struct tdls_txmgmt txmgmt;

	if (rtw_tdls_is_setup_allowed(padapter) == _false)
		goto exit;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	psa = get_sa(ptr);

	if (ptdlsinfo->sta_maximum == _true) {
		if (ptdls_sta == null)
			goto exit;
		else if (!(ptdls_sta->tdls_sta_state & tdls_linked_state))
			goto exit;
	}

	pmyid = adapter_mac_addr(padapter);
	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + llc_header_size + eth_type_len + payload_type_len;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - llc_header_size
			 - eth_type_len
			 - payload_type_len;

	if (ptdls_sta == null) {
		ptdls_sta = rtw_alloc_stainfo(pstapriv, psa);
		if (ptdls_sta == null)
			goto exit;
		
		ptdlsinfo->sta_cnt++;
	}
	else {
		if (ptdls_sta->tdls_sta_state & tdls_linked_state) {
			/* if the direct link is already set up */
			/* process as re-setup after tear down */
			rtw_info("re-setup a direct link\n");
		}
		/* already receiving tdls setup request */
		else if (ptdls_sta->tdls_sta_state & tdls_initiator_state) {
			rtw_info("receive duplicated tdls setup request frame in handshaking\n");
			goto exit;
		}
		/* when receiving and sending setup_req to the same link at the same time */
		/* sta with higher mac_addr would be initiator */
		else if (ptdls_sta->tdls_sta_state & tdls_responder_state) {
			rtw_info("receive setup_req after sending setup_req\n");
			for (i = 0; i < 6; i++) {
				if (*(pmyid + i) == *(psa + i)) {
				} else if (*(pmyid + i) > *(psa + i)) {
					ptdls_sta->tdls_sta_state = tdls_initiator_state;
					break;
				} else if (*(pmyid + i) < *(psa + i))
					goto exit;
			}
		}
	}

	if (ptdls_sta) {
		txmgmt.dialog_token = *(ptr + 2);	/* copy dialog token */
		txmgmt.status_code = _stats_successful_;

		/* parsing information element */
		for (j = fixed_ie; j < parsing_length;) {

			pie = (pndis_802_11_variable_ies)(ptr + j);

			switch (pie->elementid) {
			case _supportedrates_ie_:
				_rtw_memcpy(supportrate, pie->data, pie->length);
				supportratenum = pie->length;
				break;
			case _country_ie_:
				break;
			case _ext_supportedrates_ie_:
				if (supportratenum < sizeof(supportrate)) {
					_rtw_memcpy(supportrate + supportratenum, pie->data, pie->length);
					supportratenum += pie->length;
				}
				break;
			case _supported_ch_ie_:
				break;
			case _rsn_ie_2_:
				rsnie_included = 1;
				if (prx_pkt_attrib->encrypt) {
					prsnie = (u8 *)pie;
					/* check ccmp pairwise_cipher presence. */
					ppairwise_cipher = prsnie + 10;
					_rtw_memcpy(ptdls_sta->tdls_rsnie, pie->data, pie->length);
					pairwise_count = *(u16 *)(ppairwise_cipher - 2);
					for (k = 0; k < pairwise_count; k++) {
						if (_rtw_memcmp(ppairwise_cipher + 4 * k, rsn_cipher_suite_ccmp, 4) == _true)
							ccmp_included = 1;
					}

					if (ccmp_included == 0)
						txmgmt.status_code = _stats_invalid_rsnie_;
				}
				break;
			case _ext_cap_ie_:
				break;
			case _vendor_specific_ie_:
				break;
			case _ftie_:
				if (prx_pkt_attrib->encrypt)
					_rtw_memcpy(snonce, (ptr + j + 52), 32);
				break;
			case _timeout_itvl_ie_:
				if (prx_pkt_attrib->encrypt)
					timeout_interval = cpu_to_le32(*(u32 *)(ptr + j + 3));
				break;
			case _ric_descriptor_ie_:
				break;
#ifdef config_80211n_ht
			case _ht_capability_ie_:
				rtw_tdls_process_ht_cap(padapter, ptdls_sta, pie->data, pie->length);
				break;
#endif
#ifdef coNFIG_80211AC_VHT
			case EID_AID:
				break;
			case EID_VHTCapability:
				rtw_tdls_process_vht_cap(padapter, ptdls_sta, pIE->data, pIE->Length);
				break;
#endif
			case EID_BSSCoexistence:
				break;
			case _LINK_ID_IE_:
				if (_rtw_memcmp(get_bssid(pmlmepriv), pIE->data, 6) == _FALSE)
					txmgmt.status_code = _STATS_NOT_IN_SAME_BSS_;
				break;
			default:
				break;
			}

			j += (pIE->Length + 2);

		}

		/* Check status code */
		/* If responder STA has/hasn't security on AP, but request hasn't/has RSNIE, it should reject */
		if (txmgmt.status_code == _STATS_SUCCESSFUL_) {
			if (rsnie_included && prx_pkt_attrib->encrypt == 0)
				txmgmt.status_code = _STATS_SEC_DISABLED_;
			else if (rsnie_included == 0 && prx_pkt_attrib->encrypt)
				txmgmt.status_code = _STATS_INVALID_PARAMETERS_;

#ifdef CONFIG_WFD
			/* WFD test plan version 0.18.2 test item 5.1.5 */
			/* SoUT does not use TDLS if AP uses weak security */
			if (padapter->wdinfo.wfd_tdls_enable && (rsnie_included && prx_pkt_attrib->encrypt != _AES_))
				txmgmt.status_code = _STATS_SEC_DISABLED_;
#endif /* CONFIG_WFD */
		}

		ptdls_sta->tdls_sta_state |= TDLS_INITIATOR_STATE;
		if (prx_pkt_attrib->encrypt) {
			_rtw_memcpy(ptdls_sta->SNonce, SNonce, 32);

			if (timeout_interval <= 300)
				ptdls_sta->TDLS_PeerKey_Lifetime = TDLS_TPK_RESEND_COUNT;
			else
				ptdls_sta->TDLS_PeerKey_Lifetime = timeout_interval;
		}

		/* Update station supportRate */
		ptdls_sta->bssratelen = supportRateNum;
		_rtw_memcpy(ptdls_sta->bssrateset, supportRate, supportRateNum);

		/* -2: AP + BC/MC sta, -4: default key */
		if (ptdlsinfo->sta_cnt == MAX_ALLOWED_TDLS_STA_NUM)
			ptdlsinfo->sta_maximum = _TRUE;

#ifdef CONFIG_WFD
		rtw_tdls_process_wfd_ie(ptdlsinfo, ptr + FIXED_IE, parsing_length);
#endif

	} else
		goto exit;

	_rtw_memcpy(txmgmt.peer, prx_pkt_attrib->src, ETH_ALEN);

	if (rtw_tdls_is_driver_setup(padapter)) {
		issue_tdls_setup_rsp(padapter, &txmgmt);

		if (txmgmt.status_code == _STATS_SUCCESSFUL_)
			_set_timer(&ptdls_sta->handshake_timer, TDLS_HANDSHAKE_TIME);
		else {
			rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
		}
	}

exit:

	return _SUCCESS;
}

int On_TDLS_Setup_Rsp(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa;
	u16 status_code = 0;
	sint parsing_length;	/* Frame body length, without icv_len */
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 7;
	u8 ANonce[32];
	u8  *pftie = NULL, *ptimeout_ie = NULL, *plinkid_ie = NULL, *prsnie = NULL, *pftie_mic = NULL, *ppairwise_cipher = NULL;
	u16 pairwise_count, j, k;
	u8 verify_ccmp = 0;
	unsigned char		supportRate[16];
	int				supportRateNum = 0;
	struct tdls_txmgmt txmgmt;
	int ret = _SUCCESS;
	u32 timeout_interval = TDLS_TPK_RESEND_COUNT;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	psa = get_sa(ptr);

	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - LLC_HEADER_SIZE
			 - ETH_TYPE_LEN
			 - PAYLOAD_TYPE_LEN;

	_rtw_memcpy(&status_code, ptr + 2, 2);

	if (status_code != 0) {
		RTW_INFO("[TDLS] %s status_code = %d, free_tdls_sta\n", __FUNCTION__, status_code);
		rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
		ret = _FAIL;
		goto exit;
	}

	status_code = 0;

	/* parsing information element */
	for (j = FIXED_IE; j < parsing_length;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr + j);

		switch (pIE->ElementID) {
		case _SUPPORTEDRATES_IE_:
			_rtw_memcpy(supportRate, pIE->data, pIE->Length);
			supportRateNum = pIE->Length;
			break;
		case _COUNTRY_IE_:
			break;
		case _EXT_SUPPORTEDRATES_IE_:
			if (supportRateNum < sizeof(supportRate)) {
				_rtw_memcpy(supportRate + supportRateNum, pIE->data, pIE->Length);
				supportRateNum += pIE->Length;
			}
			break;
		case _SUPPORTED_CH_IE_:
			break;
		case _RSN_IE_2_:
			prsnie = (u8 *)pIE;
			/* Check CCMP pairwise_cipher presence. */
			ppairwise_cipher = prsnie + 10;
			_rtw_memcpy(&pairwise_count, (u16 *)(ppairwise_cipher - 2), 2);
			for (k = 0; k < pairwise_count; k++) {
				if (_rtw_memcmp(ppairwise_cipher + 4 * k, RSN_CIPHER_SUITE_CCMP, 4) == _TRUE)
					verify_ccmp = 1;
			}
		case _EXT_CAP_IE_:
			break;
		case _VENDOR_SPECIFIC_IE_:
			if (_rtw_memcmp((u8 *)pIE + 2, WMM_INFO_OUI, 6) == _TRUE) {
				/* WMM Info ID and OUI */
				if ((pregistrypriv->wmm_enable == _TRUE) || (padapter->mlmepriv.htpriv.ht_option == _TRUE))
					ptdls_sta->qos_option = _TRUE;
			}
			break;
		case _FTIE_:
			pftie = (u8 *)pIE;
			_rtw_memcpy(ANonce, (ptr + j + 20), 32);
			break;
		case _TIMEOUT_ITVL_IE_:
			ptimeout_ie = (u8 *)pIE;
			timeout_interval = cpu_to_le32(*(u32 *)(ptimeout_ie + 3));
			break;
		case _RIC_Descriptor_IE_:
			break;
		case _HT_CAPABILITY_IE_:
			rtw_tdls_process_ht_cap(padapter, ptdls_sta, pIE->data, pIE->Length);
			break;
		case EID_BSSCoexistence:
			break;
		case _LINK_ID_IE_:
			plinkid_ie = (u8 *)pIE;
			break;
		default:
			break;
		}

		j += (pIE->Length + 2);

	}

	ptdls_sta->bssratelen = supportRateNum;
	_rtw_memcpy(ptdls_sta->bssrateset, supportRate, supportRateNum);
	_rtw_memcpy(ptdls_sta->ANonce, ANonce, 32);

#ifdef CONFIG_WFD
	rtw_tdls_process_wfd_ie(ptdlsinfo, ptr + FIXED_IE, parsing_length);
#endif

	if (prx_pkt_attrib->encrypt) {
		if (verify_ccmp == 1) {
			txmgmt.status_code = _STATS_SUCCESSFUL_;
			if (rtw_tdls_is_driver_setup(padapter) == _TRUE) {
				wpa_tdls_generate_tpk(padapter, ptdls_sta);
				if (tdls_verify_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie) == _FAIL) {
					RTW_INFO("[TDLS] %s tdls_verify_mic fail, free_tdls_sta\n", __FUNCTION__);
					rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
					rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
					ret = _FAIL;
					goto exit;
				}
				ptdls_sta->TDLS_PeerKey_Lifetime = timeout_interval;
			}
		} else
			txmgmt.status_code = _STATS_INVALID_RSNIE_;
	} else
		txmgmt.status_code = _STATS_SUCCESSFUL_;

	if (rtw_tdls_is_driver_setup(padapter) == _TRUE) {
		_rtw_memcpy(txmgmt.peer, prx_pkt_attrib->src, ETH_ALEN);
		issue_tdls_setup_cfm(padapter, &txmgmt);

		if (txmgmt.status_code == _STATS_SUCCESSFUL_) {
			rtw_tdls_set_link_established(padapter, _TRUE);

			if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE) {
				ptdls_sta->tdls_sta_state |= TDLS_LINKED_STATE;
				ptdls_sta->state |= _FW_LINKED;
				_cancel_timer_ex(&ptdls_sta->handshake_timer);
			}

			if (prx_pkt_attrib->encrypt)
				rtw_tdls_set_key(padapter, ptdls_sta);

			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_ESTABLISHED);

		}
	}

exit:
	if (rtw_tdls_is_driver_setup(padapter) == _TRUE)
		return ret;
	else
		return _SUCCESS;

}

int On_TDLS_Setup_Cfm(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	_irqL irqL;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	u8 *psa;
	u16 status_code = 0;
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 5;
	u8  *pftie = NULL, *ptimeout_ie = NULL, *plinkid_ie = NULL, *prsnie = NULL, *pftie_mic = NULL, *ppairwise_cipher = NULL;
	u16 j, pairwise_count;
	int ret = _SUCCESS;

	psa = get_sa(ptr);

	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - LLC_HEADER_SIZE
			 - ETH_TYPE_LEN
			 - PAYLOAD_TYPE_LEN;

	_rtw_memcpy(&status_code, ptr + 2, 2);

	if (status_code != 0) {
		RTW_INFO("[%s] status_code = %d\n, free_tdls_sta", __FUNCTION__, status_code);
		rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
		ret = _FAIL;
		goto exit;
	}

	/* Parsing information element */
	for (j = FIXED_IE; j < parsing_length;) {

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr + j);

		switch (pIE->ElementID) {
		case _RSN_IE_2_:
			prsnie = (u8 *)pIE;
			break;
		case _VENDOR_SPECIFIC_IE_:
			if (_rtw_memcmp((u8 *)pIE + 2, WMM_PARA_OUI, 6) == _TRUE) {
				/* WMM Parameter ID and OUI */
				ptdls_sta->qos_option = _TRUE;
			}
			break;
		case _FTIE_:
			pftie = (u8 *)pIE;
			break;
		case _TIMEOUT_ITVL_IE_:
			ptimeout_ie = (u8 *)pIE;
			break;
		case _HT_EXTRA_INFO_IE_:
			break;
		case _LINK_ID_IE_:
			plinkid_ie = (u8 *)pIE;
			break;
		default:
			break;
		}

		j += (pIE->Length + 2);

	}

	if (prx_pkt_attrib->encrypt) {
		/* Verify mic in FTIE MIC field */
		if (rtw_tdls_is_driver_setup(padapter) &&
		    (tdls_verify_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie) == _FAIL)) {
			rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
			ret = _FAIL;
			goto exit;
		}
	}

	if (rtw_tdls_is_driver_setup(padapter)) {
		rtw_tdls_set_link_established(padapter, _TRUE);

		if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE) {
			ptdls_sta->tdls_sta_state |= TDLS_LINKED_STATE;
			ptdls_sta->state |= _FW_LINKED;
			_cancel_timer_ex(&ptdls_sta->handshake_timer);
		}

		if (prx_pkt_attrib->encrypt) {
			rtw_tdls_set_key(padapter, ptdls_sta);

			/* Start  TPK timer */
			ptdls_sta->TPK_count = 0;
			_set_timer(&ptdls_sta->TPK_timer, ONE_SEC);
		}

		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_ESTABLISHED);
	}

exit:
	return ret;

}

int On_TDLS_Dis_Req(_adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta_ap;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	sint parsing_length;	/* Frame body length, without icv_len */
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 3, *dst;
	u16 j;
	struct tdls_txmgmt txmgmt;
	int ret = _SUCCESS;

	if (rtw_tdls_is_driver_setup(padapter) == _FALSE)
		goto exit;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	txmgmt.dialog_token = *(ptr + 2);
	_rtw_memcpy(&txmgmt.peer, precv_frame->u.hdr.attrib.src, ETH_ALEN);
	txmgmt.action_code = TDLS_DISCOVERY_RESPONSE;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - LLC_HEADER_SIZE
			 - ETH_TYPE_LEN
			 - PAYLOAD_TYPE_LEN;

	/* Parsing information element */
	for (j = FIXED_IE; j < parsing_length;) {

		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr + j);

		switch (pIE->ElementID) {
		case _LINK_ID_IE_:
			psta_ap = rtw_get_stainfo(pstapriv, pIE->data);
			if (psta_ap == NULL)
				goto exit;
			dst = pIE->data + 12;
			if (MacAddr_isBcst(dst) == _FALSE && (_rtw_memcmp(adapter_mac_addr(padapter), dst, 6) == _FALSE))
				goto exit;
			break;
		default:
			break;
		}

		j += (pIE->Length + 2);

	}

	issue_tdls_dis_rsp(padapter, &txmgmt, prx_pkt_attrib->privacy);

exit:
	return ret;

}

int On_TDLS_Teardown(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv	*pstapriv = &padapter->stapriv;
	_irqL irqL;
	u8 reason;

	reason = *(ptr + prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN + 2);
	RTW_INFO("[TDLS] %s Reason code(%d)\n", __FUNCTION__, reason);

	if (rtw_tdls_is_driver_setup(padapter)) {
		rtw_tdls_teardown_pre_hdl(padapter, ptdls_sta);
		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY_POST);
	}

	return _SUCCESS;

}

#if 0
u8 TDLS_check_ch_state(uint state)
{
	if (state & TDLS_CH_SWITCH_ON_STATE &&
	    state & TDLS_PEER_AT_OFF_STATE) {
		if (state & TDLS_PEER_SLEEP_STATE)
			return 2;	/* U-APSD + ch. switch */
		else
			return 1;	/* ch. switch */
	} else
		return 0;
}
#endif

int On_TDLS_Peer_Traffic_Indication(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct tdls_txmgmt txmgmt;

	ptr += pattrib->hdrlen + pattrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));

		txmgmt.dialog_token = *(ptr + 2);
		issue_tdls_peer_traffic_rsp(padapter, ptdls_sta, &txmgmt);
		/* issue_nulldata_to_TDLS_peer_STA(padapter, ptdls_sta->cmn.mac_addr, 0, 0, 0); */

	return _SUCCESS;
}

/* We process buffered data for 1. U-APSD, 2. ch. switch, 3. U-APSD + ch. switch here */
int On_TDLS_Peer_Traffic_Rsp(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 wmmps_ac = 0;
	/* u8 state=TDLS_check_ch_state(ptdls_sta->tdls_sta_state); */
	int i;

	ptdls_sta->sta_stats.rx_data_pkts++;

	ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);

	/* Check 4-AC queue bit */
	if (ptdls_sta->uapsd_vo || ptdls_sta->uapsd_vi || ptdls_sta->uapsd_be || ptdls_sta->uapsd_bk)
		wmmps_ac = 1;

	/* If it's a direct link and have buffered frame */
	if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) {
		if (wmmps_ac) {
			_irqL irqL;
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe = NULL;

			_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);

			xmitframe_phead = get_list_head(&ptdls_sta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			/* transmit buffered frames */
			while (rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist) == _FALSE) {
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
				xmitframe_plist = get_next(xmitframe_plist);
				rtw_list_delete(&pxmitframe->list);

				ptdls_sta->sleepq_len--;
				ptdls_sta->sleepq_ac_len--;
				if (ptdls_sta->sleepq_len > 0) {
					pxmitframe->attrib.mdata = 1;
					pxmitframe->attrib.eosp = 0;
				} else {
					pxmitframe->attrib.mdata = 0;
					pxmitframe->attrib.eosp = 1;
				}
				pxmitframe->attrib.triggered = 1;

				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
			}

			if (ptdls_sta->sleepq_len == 0)
				RTW_INFO("no buffered packets for tdls to xmit\n");
			else {
				RTW_INFO("error!psta->sleepq_len=%d\n", ptdls_sta->sleepq_len);
				ptdls_sta->sleepq_len = 0;
			}

			_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);

		}

	}

	return _SUCCESS;
}

#ifdef CONFIG_TDLS_CH_SW
sint On_TDLS_Ch_Switch_Req(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 4;
	u16 j;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8 zaddr[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	u16 switch_time = TDLS_CH_SWITCH_TIME * 1000, switch_timeout = TDLS_CH_SWITCH_TIMEOUT * 1000;
	u8 take_care_iqk;

	if (rtw_tdls_is_chsw_allowed(padapter) == _FALSE) {
		RTW_INFO("[TDLS] Ignore %s since channel switch is not allowed\n", __func__);
		return _FAIL;
	}

	ptdls_sta->ch_switch_time = switch_time;
	ptdls_sta->ch_switch_timeout = switch_timeout;

	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - LLC_HEADER_SIZE
			 - ETH_TYPE_LEN
			 - PAYLOAD_TYPE_LEN;

	pchsw_info->off_ch_num = *(ptr + 2);

	if ((*(ptr + 2) == 2) && (hal_is_band_support(padapter, BAND_ON_5G)))
		pchsw_info->off_ch_num = 44;

	if (pchsw_info->off_ch_num != pmlmeext->cur_channel)
		pchsw_info->delay_switch_back = _FALSE;

	/* Parsing information element */
	for (j = FIXED_IE; j < parsing_length;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr + j);

		switch (pIE->ElementID) {
		case EID_SecondaryChnlOffset:
			switch (*(pIE->data)) {
			case EXTCHNL_OFFSET_UPPER:
				pchsw_info->ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;

			case EXTCHNL_OFFSET_LOWER:
				pchsw_info->ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;

			default:
				pchsw_info->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
			}
			break;
		case _LINK_ID_IE_:
			break;
		case _CH_SWITCH_TIMING_:
			ptdls_sta->ch_switch_time = (RTW_GET_LE16(pIE->data) >= TDLS_CH_SWITCH_TIME * 1000) ?
				RTW_GET_LE16(pIE->data) : TDLS_CH_SWITCH_TIME * 1000;
			ptdls_sta->ch_switch_timeout = (RTW_GET_LE16(pIE->data + 2) >= TDLS_CH_SWITCH_TIMEOUT * 1000) ?
				RTW_GET_LE16(pIE->data + 2) : TDLS_CH_SWITCH_TIMEOUT * 1000;
			RTW_INFO("[TDLS] %s ch_switch_time:%d, ch_switch_timeout:%d\n"
				, __FUNCTION__, RTW_GET_LE16(pIE->data), RTW_GET_LE16(pIE->data + 2));
		default:
			break;
		}

		j += (pIE->Length + 2);
	}

	rtw_hal_get_hwreg(padapter, HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO, &take_care_iqk);
	if (take_care_iqk == _TRUE) {
		u8 central_chnl;
		u8 bw_mode;

		bw_mode = (pchsw_info->ch_offset) ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20;
		central_chnl = rtw_get_center_ch(pchsw_info->off_ch_num, bw_mode, pchsw_info->ch_offset);
		if (rtw_hal_ch_sw_iqk_info_search(padapter, central_chnl, bw_mode) < 0) {
			if (!(pchsw_info->ch_sw_state & TDLS_CH_SWITCH_PREPARE_STATE))
				rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_PREPARE);

			return _FAIL;
		}
	}

	/* cancel ch sw monitor timer for responder */
	if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
		_cancel_timer_ex(&ptdls_sta->ch_sw_monitor_timer);

	if (_rtw_memcmp(pchsw_info->addr, zaddr, ETH_ALEN) == _TRUE)
		_rtw_memcpy(pchsw_info->addr, ptdls_sta->cmn.mac_addr, ETH_ALEN);

	if (ATOMIC_READ(&pchsw_info->chsw_on) == _FALSE)
		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_START);

	rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_RESP);

	return _SUCCESS;
}

sint On_TDLS_Ch_Switch_Rsp(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta)
{
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*prx_pkt_attrib = &precv_frame->u.hdr.attrib;
	sint parsing_length;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 FIXED_IE = 4;
	u16 status_code, j, switch_time, switch_timeout;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int ret = _SUCCESS;

	if (rtw_tdls_is_chsw_allowed(padapter) == _FALSE) {
		RTW_INFO("[TDLS] Ignore %s since channel switch is not allowed\n", __func__);
		return _SUCCESS;
	}

	/* If we receive Unsolicited TDLS Channel Switch Response when channel switch is running, */
	/* we will go back to base channel and terminate this channel switch procedure */
	if (ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE) {
		if (pmlmeext->cur_channel != rtw_get_oper_ch(padapter)) {
			RTW_INFO("[TDLS] Rx unsolicited channel switch response\n");
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_TO_BASE_CHNL);
			goto exit;
		}
	}

	ptr += prx_pkt_attrib->hdrlen + prx_pkt_attrib->iv_len + LLC_HEADER_SIZE + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;
	parsing_length = ((union recv_frame *)precv_frame)->u.hdr.len
			 - prx_pkt_attrib->hdrlen
			 - prx_pkt_attrib->iv_len
			 - prx_pkt_attrib->icv_len
			 - LLC_HEADER_SIZE
			 - ETH_TYPE_LEN
			 - PAYLOAD_TYPE_LEN;

	_rtw_memcpy(&status_code, ptr + 2, 2);

	if (status_code != 0) {
		RTW_INFO("[TDLS] %s status_code:%d\n", __func__, status_code);
		pchsw_info->ch_sw_state &= ~(TDLS_CH_SW_INITIATOR_STATE);
		rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_END);
		ret = _FAIL;
		goto exit;
	}

	/* Parsing information element */
	for (j = FIXED_IE; j < parsing_length;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(ptr + j);

		switch (pIE->ElementID) {
		case _LINK_ID_IE_:
			break;
		case _CH_SWITCH_TIMING_:
			_rtw_memcpy(&switch_time, pIE->data, 2);
			if (switch_time > ptdls_sta->ch_switch_time)
				_rtw_memcpy(&ptdls_sta->ch_switch_time, &switch_time, 2);

			_rtw_memcpy(&switch_timeout, pIE->data + 2, 2);
			if (switch_timeout > ptdls_sta->ch_switch_timeout)
				_rtw_memcpy(&ptdls_sta->ch_switch_timeout, &switch_timeout, 2);
			break;
		default:
			break;
		}

		j += (pIE->Length + 2);
	}

	if ((pmlmeext->cur_channel == rtw_get_oper_ch(padapter)) &&
	    (pchsw_info->ch_sw_state & TDLS_WAIT_CH_RSP_STATE)) {
		if (ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE)
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_TO_OFF_CHNL);
	}

exit:
	return ret;
}
#endif /* CONFIG_TDLS_CH_SW */

#ifdef CONFIG_WFD
void wfd_ie_tdls(_adapter *padapter, u8 *pframe, u32 *pktlen)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wifi_display_info	*pwfd_info = padapter->tdlsinfo.wfd_info;
	u8 wfdie[MAX_WFD_IE_LEN] = { 0x00 };
	u32 wfdielen = 0;
	u16 v16 = 0;

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST))
		return;

	/* WFD OUI */
	wfdielen = 0;
	wfdie[wfdielen++] = 0x50;
	wfdie[wfdielen++] = 0x6F;
	wfdie[wfdielen++] = 0x9A;
	wfdie[wfdielen++] = 0x0A;	/* WFA WFD v1.0 */

	/*
	 *	Commented by Albert 20110825
	 *	According to the WFD Specification, the negotiation request frame should contain 3 WFD attributes
	 *	1. WFD Device Information
	 *	2. Associated BSSID ( Optional )
	 *	3. Local IP Adress ( Optional )
	 */

	/* WFD Device Information ATTR */
	/* Type: */
	wfdie[wfdielen++] = WFD_ATTR_DEVICE_INFO;

	/* Length: */
	/* Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/* Value1: */
	/* WFD device information */
	/* available for WFD session + Preferred TDLS + WSD ( WFD Service Discovery ) */
	v16 = pwfd_info->wfd_device_type | WFD_DEVINFO_SESSION_AVAIL
		| WFD_DEVINFO_PC_TDLS | WFD_DEVINFO_WSD;
	RTW_PUT_BE16(wfdie + wfdielen, v16);
	wfdielen += 2;

	/* Value2: */
	/* Session Management Control Port */
	/* Default TCP port for RTSP messages is 554 */
	RTW_PUT_BE16(wfdie + wfdielen, pwfd_info->tdls_rtsp_ctrlport);
	wfdielen += 2;

	/* Value3: */
	/* WFD Device Maximum Throughput */
	/* 300Mbps is the maximum throughput */
	RTW_PUT_BE16(wfdie + wfdielen, 300);
	wfdielen += 2;

	/* Associated BSSID ATTR */
	/* Type: */
	wfdie[wfdielen++] = WFD_ATTR_ASSOC_BSSID;

	/* Length: */
	/* Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0006);
	wfdielen += 2;

	/* Value: */
	/* Associated BSSID */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
		_rtw_memcpy(wfdie + wfdielen, &pmlmepriv->assoc_bssid[0], ETH_ALEN);
	else
		_rtw_memset(wfdie + wfdielen, 0x00, ETH_ALEN);

	/* Local IP Address ATTR */
	wfdie[wfdielen++] = WFD_ATTR_LOCAL_IP_ADDR;

	/* Length: */
	/* Note: In the WFD specification, the size of length field is 2. */
	RTW_PUT_BE16(wfdie + wfdielen, 0x0005);
	wfdielen += 2;

	/* Version: */
	/* 0x01: Version1;IPv4 */
	wfdie[wfdielen++] = 0x01;

	/* IPv4 Address */
	_rtw_memcpy(wfdie + wfdielen, pwfd_info->ip_address, 4);
	wfdielen += 4;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wfdielen, (unsigned char *) wfdie, pktlen);

}
#endif /* CONFIG_WFD */

void rtw_build_tdls_setup_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	int i = 0 ;
	u32 time;
	u8 *pframe_head;

	/* SNonce */
	if (pattrib->encrypt) {
		for (i = 0; i < 8; i++) {
			time = rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->SNonce[4 * i], (u8 *)&time, 4);
		}
	}

	pframe_head = pframe;	/* For rtw_tdls_set_ht_cap() */

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);

	pframe = rtw_tdls_set_capability(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_supported_rate(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_sup_ch(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_sup_reg_class(pframe, pattrib);

	if (pattrib->encrypt)
		pframe = rtw_tdls_set_rsnie(ptxmgmt, pframe, pattrib,  _TRUE, ptdls_sta);

	pframe = rtw_tdls_set_ext_cap(pframe, pattrib);

	if (pattrib->encrypt) {
		pframe = rtw_tdls_set_ftie(ptxmgmt
					   , pframe
					   , pattrib
					   , NULL
					   , ptdls_sta->SNonce);

		pframe = rtw_tdls_set_timeout_interval(ptxmgmt, pframe, pattrib, _TRUE, ptdls_sta);
	}

	/* Sup_reg_classes(optional) */
	if (pregistrypriv->ht_enable == _TRUE)
		pframe = rtw_tdls_set_ht_cap(padapter, pframe_head, pattrib);

	pframe = rtw_tdls_set_bss_coexist(padapter, pframe, pattrib);

	pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	if ((pregistrypriv->wmm_enable == _TRUE) || (padapter->mlmepriv.htpriv.ht_option == _TRUE))
		pframe = rtw_tdls_set_qos_cap(pframe, pattrib);

#ifdef CONFIG_WFD
	if (padapter->wdinfo.wfd_tdls_enable == 1)
		wfd_ie_tdls(padapter, pframe, &(pattrib->pktlen));
#endif

}

void rtw_build_tdls_setup_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 k; /* for random ANonce */
	u8  *pftie = NULL, *ptimeout_ie = NULL, *plinkid_ie = NULL, *prsnie = NULL, *pftie_mic = NULL;
	u32 time;
	u8 *pframe_head;

	if (pattrib->encrypt) {
		for (k = 0; k < 8; k++) {
			time = rtw_get_current_time();
			_rtw_memcpy(&ptdls_sta->ANonce[4 * k], (u8 *)&time, 4);
		}
	}

	pframe_head = pframe;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_status_code(pframe, pattrib, ptxmgmt);

	if (ptxmgmt->status_code != 0) {
		RTW_INFO("[%s] status_code:%04x\n", __FUNCTION__, ptxmgmt->status_code);
		return;
	}

	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_capability(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_supported_rate(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_sup_ch(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_sup_reg_class(pframe, pattrib);

	if (pattrib->encrypt) {
		prsnie = pframe;
		pframe = rtw_tdls_set_rsnie(ptxmgmt, pframe, pattrib,  _FALSE, ptdls_sta);
	}

	pframe = rtw_tdls_set_ext_cap(pframe, pattrib);

	if (pattrib->encrypt) {
		if (rtw_tdls_is_driver_setup(padapter) == _TRUE)
			wpa_tdls_generate_tpk(padapter, ptdls_sta);

		pftie = pframe;
		pftie_mic = pframe + 4;
		pframe = rtw_tdls_set_ftie(ptxmgmt
					   , pframe
					   , pattrib
					   , ptdls_sta->ANonce
					   , ptdls_sta->SNonce);

		ptimeout_ie = pframe;
		pframe = rtw_tdls_set_timeout_interval(ptxmgmt, pframe, pattrib, _FALSE, ptdls_sta);
	}

	/* Sup_reg_classes(optional) */
	if (pregistrypriv->ht_enable == _TRUE)
		pframe = rtw_tdls_set_ht_cap(padapter, pframe_head, pattrib);

	pframe = rtw_tdls_set_bss_coexist(padapter, pframe, pattrib);

	plinkid_ie = pframe;
	pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);

	/* Fill FTIE mic */
	if (pattrib->encrypt && rtw_tdls_is_driver_setup(padapter) == _TRUE)
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 2, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);

	if ((pregistrypriv->wmm_enable == _TRUE) || (padapter->mlmepriv.htpriv.ht_option == _TRUE))
		pframe = rtw_tdls_set_qos_cap(pframe, pattrib);

#ifdef CONFIG_WFD
	if (padapter->wdinfo.wfd_tdls_enable)
		wfd_ie_tdls(padapter, pframe, &(pattrib->pktlen));
#endif

}

void rtw_build_tdls_setup_cfm_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	unsigned int ie_len;
	unsigned char *p;
	u8 wmm_param_ele[24] = {0};
	u8  *pftie = NULL, *ptimeout_ie = NULL, *plinkid_ie = NULL, *prsnie = NULL, *pftie_mic = NULL;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_status_code(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);

	if (ptxmgmt->status_code != 0)
		return;

	if (pattrib->encrypt) {
		prsnie = pframe;
		pframe = rtw_tdls_set_rsnie(ptxmgmt, pframe, pattrib, _TRUE, ptdls_sta);
	}

	if (pattrib->encrypt) {
		pftie = pframe;
		pftie_mic = pframe + 4;
		pframe = rtw_tdls_set_ftie(ptxmgmt
					   , pframe
					   , pattrib
					   , ptdls_sta->ANonce
					   , ptdls_sta->SNonce);

		ptimeout_ie = pframe;
		pframe = rtw_tdls_set_timeout_interval(ptxmgmt, pframe, pattrib, _TRUE, ptdls_sta);

		if (rtw_tdls_is_driver_setup(padapter) == _TRUE) {
			/* Start TPK timer */
			ptdls_sta->TPK_count = 0;
			_set_timer(&ptdls_sta->TPK_timer, ONE_SEC);
		}
	}

	/* HT operation; todo */

	plinkid_ie = pframe;
	pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	if (pattrib->encrypt && (rtw_tdls_is_driver_setup(padapter) == _TRUE))
		wpa_tdls_ftie_mic(ptdls_sta->tpk.kck, 3, plinkid_ie, prsnie, ptimeout_ie, pftie, pftie_mic);

	if (ptdls_sta->qos_option == _TRUE)
		pframe = rtw_tdls_set_wmm_params(padapter, pframe, pattrib);
}

void rtw_build_tdls_teardown_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8  *pftie = NULL, *pftie_mic = NULL, *plinkid_ie = NULL;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_status_code(pframe, pattrib, ptxmgmt);

	if (pattrib->encrypt) {
		pftie = pframe;
		pftie_mic = pframe + 4;
		pframe = rtw_tdls_set_ftie(ptxmgmt
					   , pframe
					   , pattrib
					   , ptdls_sta->ANonce
					   , ptdls_sta->SNonce);
	}

	plinkid_ie = pframe;
	if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);
	else if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	if (pattrib->encrypt && (rtw_tdls_is_driver_setup(padapter) == _TRUE))
		wpa_tdls_teardown_ftie_mic(ptdls_sta->tpk.kck, plinkid_ie, ptxmgmt->status_code, 1, 4, pftie, pftie_mic);
}

void rtw_build_tdls_dis_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

}

void rtw_build_tdls_dis_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, u8 privacy)
{
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 *pframe_head, pktlen_index;

	pktlen_index = pattrib->pktlen;
	pframe_head = pframe;

	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_PUBLIC);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_capability(padapter, pframe, pattrib);

	pframe = rtw_tdls_set_supported_rate(padapter, pframe, pattrib);

	pframe = rtw_tdls_set_sup_ch(padapter, pframe, pattrib);

	if (privacy)
		pframe = rtw_tdls_set_rsnie(ptxmgmt, pframe, pattrib, _TRUE, NULL);

	pframe = rtw_tdls_set_ext_cap(pframe, pattrib);

	if (privacy) {
		pframe = rtw_tdls_set_ftie(ptxmgmt, pframe, pattrib, NULL, NULL);
		pframe = rtw_tdls_set_timeout_interval(ptxmgmt, pframe, pattrib,  _TRUE, NULL);
	}

	if (pregistrypriv->ht_enable == _TRUE)
		pframe = rtw_tdls_set_ht_cap(padapter, pframe_head - pktlen_index, pattrib);

	pframe = rtw_tdls_set_bss_coexist(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);

}


void rtw_build_tdls_peer_traffic_indication_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 AC_queue = 0;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);

	if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);
	else if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	/* PTI control */
	/* PU buffer status */
	if (ptdls_sta->uapsd_bk & BIT(1))
		AC_queue = BIT(0);
	if (ptdls_sta->uapsd_be & BIT(1))
		AC_queue = BIT(1);
	if (ptdls_sta->uapsd_vi & BIT(1))
		AC_queue = BIT(2);
	if (ptdls_sta->uapsd_vo & BIT(1))
		AC_queue = BIT(3);
	pframe = rtw_set_ie(pframe, _PTI_BUFFER_STATUS_, 1, &AC_queue, &(pattrib->pktlen));

}

void rtw_build_tdls_peer_traffic_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_dialog(pframe, pattrib, ptxmgmt);

	if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);
	else if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);
}

#ifdef CONFIG_TDLS_CH_SW
void rtw_build_tdls_ch_switch_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	u16 switch_time = TDLS_CH_SWITCH_TIME * 1000, switch_timeout = TDLS_CH_SWITCH_TIMEOUT * 1000;

	ptdls_sta->ch_switch_time = switch_time;
	ptdls_sta->ch_switch_timeout = switch_timeout;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_target_ch(padapter, pframe, pattrib);
	pframe = rtw_tdls_set_reg_class(pframe, pattrib, ptdls_sta);

	if (ptdlsinfo->chsw_info.ch_offset != HAL_PRIME_CHNL_OFFSET_DONT_CARE) {
		switch (ptdlsinfo->chsw_info.ch_offset) {
		case HAL_PRIME_CHNL_OFFSET_LOWER:
			pframe = rtw_tdls_set_second_channel_offset(pframe, pattrib, SCA);
			break;
		case HAL_PRIME_CHNL_OFFSET_UPPER:
			pframe = rtw_tdls_set_second_channel_offset(pframe, pattrib, SCB);
			break;
		}
	}

	if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);
	else if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	pframe = rtw_tdls_set_ch_sw(pframe, pattrib, ptdls_sta);

}

void rtw_build_tdls_ch_switch_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta)
{

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_priv	*pstapriv = &padapter->stapriv;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_tdls_set_category(pframe, pattrib, RTW_WLAN_CATEGORY_TDLS);
	pframe = rtw_tdls_set_action(pframe, pattrib, ptxmgmt);
	pframe = rtw_tdls_set_status_code(pframe, pattrib, ptxmgmt);

	if (ptdls_sta->tdls_sta_state & TDLS_INITIATOR_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _FALSE);
	else if (ptdls_sta->tdls_sta_state & TDLS_RESPONDER_STATE)
		pframe = rtw_tdls_set_linkid(padapter, pframe, pattrib, _TRUE);

	pframe = rtw_tdls_set_ch_sw(pframe, pattrib, ptdls_sta);
}
#endif

#ifdef CONFIG_WFD
void rtw_build_tunneled_probe_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe)
{
	u8 i;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct wifidirect_info *pwdinfo;

	u8 category = RTW_WLAN_CATEGORY_P2P;
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a};
	u8 probe_req = 4;
	u8 wfdielen = 0;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 3, WFA_OUI, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(probe_req), &(pattrib->pktlen));

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			pwdinfo = &iface->wdinfo;
			if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
				wfdielen = build_probe_req_wfd_ie(pwdinfo, pframe);
				pframe += wfdielen;
				pattrib->pktlen += wfdielen;
			}
		}
	}
}

void rtw_build_tunneled_probe_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe)
{
	u8 i;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct wifidirect_info *pwdinfo;
	u8 category = RTW_WLAN_CATEGORY_P2P;
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a};
	u8 probe_rsp = 5;
	u8 wfdielen = 0;

	pframe = rtw_tdls_set_payload_type(pframe, pattrib);
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 3, WFA_OUI, &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(probe_rsp), &(pattrib->pktlen));

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if ((iface) && rtw_is_adapter_up(iface)) {
			pwdinfo = &iface->wdinfo;
			if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
				wfdielen = build_probe_resp_wfd_ie(pwdinfo, pframe, 1);
				pframe += wfdielen;
				pattrib->pktlen += wfdielen;
			}
		}
	}
}
#endif /* CONFIG_WFD */

void _tdls_tpk_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	ptdls_sta->TPK_count++;
	/* TPK_timer expired in a second */
	/* Retry timer should set at least 301 sec. */
	if (ptdls_sta->TPK_count >= (ptdls_sta->TDLS_PeerKey_Lifetime - 3)) {
		RTW_INFO("[TDLS] %s, Re-Setup TDLS link with "MAC_FMT" since TPK lifetime expires!\n",
			__FUNCTION__, MAC_ARG(ptdls_sta->cmn.mac_addr));
		ptdls_sta->TPK_count = 0;
		_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);
		issue_tdls_setup_req(ptdls_sta->padapter, &txmgmt, _FALSE);
	}

	_set_timer(&ptdls_sta->TPK_timer, ONE_SEC);
}

#ifdef CONFIG_TDLS_CH_SW
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void _tdls_ch_switch_timer_hdl(void *FunctionContext)
#else
void _tdls_ch_switch_timer_hdl(struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
#else
	struct sta_info *ptdls_sta = from_timer(ptdls_sta, t, _tdls_ch_switch_timer_hdl);
#endif
	_adapter *padapter = ptdls_sta->padapter;
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;

	rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_END_TO_BASE_CHNL);
	RTW_INFO("[TDLS] %s, can't get traffic from op_ch:%d\n", __func__, rtw_get_oper_ch(padapter));
}

void _tdls_delay_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = ptdls_sta->padapter;
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;

	RTW_INFO("[TDLS] %s, op_ch:%d, tdls_state:0x%08x\n", __func__, rtw_get_oper_ch(padapter), ptdls_sta->tdls_sta_state);
	pchsw_info->delay_switch_back = _TRUE;
}

void _tdls_stay_on_base_chnl_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = ptdls_sta->padapter;
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;

	if (ptdls_sta != NULL) {
		issue_tdls_ch_switch_req(padapter, ptdls_sta);
		pchsw_info->ch_sw_state |= TDLS_WAIT_CH_RSP_STATE;
	}
}

void _tdls_ch_switch_monitor_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = ptdls_sta->padapter;
	struct tdls_ch_switch *pchsw_info = &padapter->tdlsinfo.chsw_info;

	rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_CH_SW_END);
	RTW_INFO("[TDLS] %s, does not receive ch sw req\n", __func__);
}

#endif

void _tdls_handshake_timer_hdl(void *FunctionContext)
{
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
	_adapter *padapter = NULL;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);
	txmgmt.status_code = _RSON_TDLS_TEAR_UN_RSN_;

	if (ptdls_sta != NULL) {
		padapter = ptdls_sta->padapter;

		RTW_INFO("[TDLS] Handshake time out\n");
		if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE)
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA);
		else
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA_LOCALLY);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void _tdls_pti_timer_hdl(void *FunctionContext)
#else
void _tdls_pti_timer_hdl(struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	struct sta_info *ptdls_sta = (struct sta_info *)FunctionContext;
#else
	struct sta_info *ptdls_sta = from_timer(ptdls_sta, t, _tdls_tpk_timer_hdl);
#endif
	_adapter *padapter = NULL;
	struct tdls_txmgmt txmgmt;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memcpy(txmgmt.peer, ptdls_sta->cmn.mac_addr, ETH_ALEN);
	txmgmt.status_code = _RSON_TDLS_TEAR_TOOFAR_;

	if (ptdls_sta != NULL) {
		padapter = ptdls_sta->padapter;

		if (ptdls_sta->tdls_sta_state & TDLS_WAIT_PTR_STATE) {
			RTW_INFO("[TDLS] Doesn't receive PTR from peer dev:"MAC_FMT"; "
				"Send TDLS Tear Down\n", MAC_ARG(ptdls_sta->cmn.mac_addr));
			rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_TEARDOWN_STA);
		}
	}
}

void rtw_init_tdls_timer(_adapter *padapter, struct sta_info *psta)
{
	psta->padapter = padapter;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	rtw_init_timer(&psta->TPK_timer, padapter, _tdls_tpk_timer_hdl, psta);
#ifdef CONFIG_TDLS_CH_SW
	rtw_init_timer(&psta->ch_sw_timer, padapter, _tdls_ch_switch_timer_hdl, psta);
	rtw_init_timer(&psta->delay_timer, padapter, _tdls_delay_timer_hdl, psta);
	rtw_init_timer(&psta->stay_on_base_chnl_timer, padapter, _tdls_stay_on_base_chnl_timer_hdl, psta);
	rtw_init_timer(&psta->ch_sw_monitor_timer, padapter, _tdls_ch_switch_monitor_timer_hdl, psta);
#endif
	rtw_init_timer(&psta->handshake_timer, padapter, _tdls_handshake_timer_hdl, psta);
	rtw_init_timer(&psta->pti_timer, padapter, _tdls_pti_timer_hdl, psta);
#else
	timer_setup(&psta->TPK_timer, _tdls_tpk_timer_hdl, 0);
#ifdef CONFIG_TDLS_CH_SW
	timer_setup(&psta->ch_sw_timer, _tdls_ch_switch_timer_hdl, 0);
	timer_setup(&psta->delay_timer, _tdls_delay_timer_hdl, 0);
	timer_setup(&psta->stay_on_base_chnl_timer, _tdls_stay_on_base_chnl_timer_hdl, 0);
	timer_setup(&psta->ch_sw_monitor_timer, _tdls_ch_switch_monitor_timer_hdl, 0);
#endif
	timer_setup(&psta->handshake_timer, _tdls_handshake_timer_hdl, 0);
	timer_setup(&psta->pti_timer, _tdls_pti_timer_hdl, 0);
#endif
}

void rtw_cancel_tdls_timer(struct sta_info *psta)
{
	_cancel_timer_ex(&psta->TPK_timer);
#ifdef CONFIG_TDLS_CH_SW
	_cancel_timer_ex(&psta->ch_sw_timer);
	_cancel_timer_ex(&psta->delay_timer);
	_cancel_timer_ex(&psta->stay_on_base_chnl_timer);
	_cancel_timer_ex(&psta->ch_sw_monitor_timer);
#endif
	_cancel_timer_ex(&psta->handshake_timer);
	_cancel_timer_ex(&psta->pti_timer);
}

void rtw_tdls_teardown_pre_hdl(_adapter *padapter, struct sta_info *psta)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_priv *pstapriv = &padapter->stapriv;
	_irqL irqL;

	rtw_cancel_tdls_timer(psta);

	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	if (ptdlsinfo->sta_cnt != 0)
		ptdlsinfo->sta_cnt--;
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);

	if (ptdlsinfo->sta_cnt < MAX_ALLOWED_TDLS_STA_NUM) {
		ptdlsinfo->sta_maximum = _FALSE;
		_rtw_memset(&ptdlsinfo->ss_record, 0x00, sizeof(struct tdls_ss_record));
	}

	if (ptdlsinfo->sta_cnt == 0)
		rtw_tdls_set_link_established(padapter, _FALSE);
	else
		RTW_INFO("Remain tdls sta:%02x\n", ptdlsinfo->sta_cnt);
}

void rtw_tdls_teardown_post_hdl(_adapter *padapter, struct sta_info *psta, u8 enqueue_cmd)
{
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;

	/* Clear cam */
	rtw_clearstakey_cmd(padapter, psta, enqueue_cmd);

	/* Update sta media status */
	if (enqueue_cmd)
		rtw_sta_media_status_rpt_cmd(padapter, psta, 0);
	else
		rtw_sta_media_status_rpt(padapter, psta, 0);

	/* Set RCR if necessary */
	if (ptdlsinfo->sta_cnt == 0) {
		if (enqueue_cmd)
			rtw_tdls_cmd(padapter, NULL, TDLS_RS_RCR);
		else
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_TDLS_NOLINK);
	}

	/* Free tdls sta info */
	rtw_free_stainfo(padapter,  psta);
}

int rtw_tdls_is_driver_setup(_adapter *padapter)
{
	return padapter->tdlsinfo.driver_setup;
}

const char *rtw_tdls_action_txt(enum TDLS_ACTION_FIELD action)
{
	switch (action) {
	case TDLS_SETUP_REQUEST:
		return "TDLS_SETUP_REQUEST";
	case TDLS_SETUP_RESPONSE:
		return "TDLS_SETUP_RESPONSE";
	case TDLS_SETUP_CONFIRM:
		return "TDLS_SETUP_CONFIRM";
	case TDLS_TEARDOWN:
		return "TDLS_TEARDOWN";
	case TDLS_PEER_TRAFFIC_INDICATION:
		return "TDLS_PEER_TRAFFIC_INDICATION";
	case TDLS_CHANNEL_SWITCH_REQUEST:
		return "TDLS_CHANNEL_SWITCH_REQUEST";
	case TDLS_CHANNEL_SWITCH_RESPONSE:
		return "TDLS_CHANNEL_SWITCH_RESPONSE";
	case TDLS_PEER_PSM_REQUEST:
		return "TDLS_PEER_PSM_REQUEST";
	case TDLS_PEER_PSM_RESPONSE:
		return "TDLS_PEER_PSM_RESPONSE";
	case TDLS_PEER_TRAFFIC_RESPONSE:
		return "TDLS_PEER_TRAFFIC_RESPONSE";
	case TDLS_DISCOVERY_REQUEST:
		return "TDLS_DISCOVERY_REQUEST";
	case TDLS_DISCOVERY_RESPONSE:
		return "TDLS_DISCOVERY_RESPONSE";
	default:
		return "UNKNOWN";
	}
}

#endif /* CONFIG_TDLS */
