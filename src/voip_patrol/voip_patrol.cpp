/*
 * Voip Patrol
 * @author Julien Chavanton 2016
 */
#include "voip_patrol.hh"
#include "action.hh"
#define THIS_FILE "voip_patrol.cpp"

using namespace pj;


void get_time_string(char * str_now) {
	time_t t = time(0);   // get time now
	struct tm * now = localtime( & t );
	sprintf(str_now,"%02d-%02d-%04d %02d:%02d:%02d", now->tm_mday, now->tm_mon+1, now->tm_year+1900, now->tm_hour, now->tm_min, now->tm_sec);
}

/*
 * TestCall implementation
 */

TestCall::TestCall(Account &p_acc, int call_id = PJSUA_INVALID_ID) : Call(p_acc, call_id) {
	test = NULL;
	acc = (TestAccount *)&p_acc;
	recorder_id = -1;
	player_id = -1;
	role = -1; // Caller 0 | callee 1
}

TestCall::~TestCall() {
	if (test) {
		LOG(logINFO) << "delete call test["<<test<<"]";
		delete test;
	}
}

void TestCall::setTest(Test *p_test) {
	test = p_test;
}

void TestCall::onCallTsxState(OnCallTsxStateParam &prm) {
	PJ_UNUSED_ARG(prm);
	CallInfo ci = getInfo();
	LOG(logDEBUG) <<"[CallTsx]["<<getId()<<"]["<<ci.remoteUri<<"]["<<ci.stateText<<"]id["<<ci.callIdString<<"]";
}

void TestCall::onStreamDestroyed(OnStreamDestroyedParam &prm) {
	LOG(logDEBUG) << "[onStreamDestroyed] idx["<<prm.streamIdx<<"]";
	pjmedia_stream const *pj_stream = (pjmedia_stream *)&prm.stream;
	pjmedia_stream_info *pj_stream_info;
	try {
		StreamStat const &stats = getStreamStat(prm.streamIdx);
		RtcpStat rtcp = stats.rtcp;
		RtcpStreamStat rxStat = rtcp.rxStat;
		RtcpStreamStat txStat = rtcp.txStat;
		LOG(logINFO) << __FUNCTION__ << ": RTCP pkt_rx:"<<rxStat.pkt<<" pkt_tx:"<<txStat.pkt<<std::endl;
	} catch (pj::Error e)  {
			LOG(logERROR) << "error :" << e.status << std::endl;
	}
}

void TestCall::onStreamCreated(OnStreamCreatedParam &prm) {
	LOG(logDEBUG) << __FUNCTION__ << " idx["<<prm.streamIdx<<"]\n";
	pjmedia_stream const *pj_stream = (pjmedia_stream *)&prm.stream;
	pjmedia_stream_info *pj_stream_info;
	pjmedia_stream_get_info(pj_stream, pj_stream_info);
}

static pj_status_t record_call(TestCall* call, pjsua_call_id call_id, const char *caller_contact) {
	pj_status_t status = PJ_SUCCESS;
	pjsua_recorder_id recorder_id;
	char rec_fn[1024] = "voice_ref_files/recording.wav";
	CallInfo ci = call->getInfo();
	sprintf(rec_fn,"voice_files/%s_%s_rec.wav", ci.callIdString.c_str(), caller_contact);
	const pj_str_t rec_file_name = pj_str(rec_fn);
	status = pjsua_recorder_create(&rec_file_name, 0, NULL, -1, 0, &recorder_id);
	if (status != PJ_SUCCESS) {
		LOG(logINFO) << "[error] tecord_call \n";
		return status;
	}
	call->recorder_id = recorder_id;
	LOG(logINFO) << "[recorder] created:" << recorder_id << " fn:"<< rec_fn;
	status = pjsua_conf_connect( pjsua_call_get_conf_port(call_id), pjsua_recorder_get_conf_port(recorder_id) );
}

static pj_status_t stream_to_call(TestCall* call, pjsua_call_id call_id, const char *caller_contact ) {
	pj_status_t status = PJ_SUCCESS;
	pjsua_player_id player_id;
	char fn[] = "voice_ref_files/reference_8000.wav";
	const pj_str_t file_name = pj_str(fn);
	status = pjsua_player_create(&file_name, 0, &player_id);
	if (status != PJ_SUCCESS) {
		LOG(logINFO) << "[error] play_call \n";
		return status;
	}
	call->player_id = player_id;
	LOG(logDEBUG) << "[player] created:" << player_id;
	status = pjsua_conf_connect( pjsua_player_get_conf_port(player_id), pjsua_call_get_conf_port(call_id) );
}

void TestCall::onCallState(OnCallStateParam &prm) {
	PJ_UNUSED_ARG(prm);

	LOG(logDEBUG) << "TestCall::onCallState";
	CallInfo ci = getInfo();

	int uri_prefix = 3; // sip:
	std::string remote_user("");
	std::string local_user("");
	std::size_t pos = ci.localUri.find("@");
	if (ci.localUri[0] == '<')
		uri_prefix++;
	if (pos!=std::string::npos) {
		local_user = ci.localUri.substr(uri_prefix, pos - uri_prefix);
	}
	pos = ci.remoteUri.find("@");
	uri_prefix = 3;
	if (ci.remoteUri[0] != '<')
		uri_prefix++;
	if (pos!=std::string::npos) {
		remote_user = ci.remoteUri.substr(uri_prefix, pos - uri_prefix);
	}
	role = ci.role;

	if (test) {
		pjsip_tx_data *pjsip_data = (pjsip_tx_data *) prm.e.body.txMsg.tdata.pjTxData;
		if (pjsip_data) {
			test->transport = pjsip_data->tp_info.transport->type_name;
			test->peer_socket = pjsip_data->tp_info.dst_name;
			test->peer_socket = test->peer_socket +":"+ std::to_string(pjsip_data->tp_info.dst_port);
		}
		if (test->state != VPT_DONE && test->wait_state && (int)test->wait_state <= (int)ci.state ) {
			test->state = VPT_RUN;
			LOG(logDEBUG) <<"[test-wait-return]";
		}
		LOG(logINFO) << "[conCallState]["<<getId()<<"]role["<<(ci.role==0?"CALLER":"CALLEE")<<"]id["<<ci.callIdString
                             <<"]["<<ci.localUri<<"]["<<ci.remoteUri<<"]["<< ci.stateText<<"|"<<ci.state<<"]";
		test->call_id = getId();
		test->sip_call_id = ci.callIdString;
	}
	if (test && (ci.state == PJSIP_INV_STATE_DISCONNECTED || ci.state == PJSIP_INV_STATE_CONFIRMED)) {
		std::string res = "call[" + std::to_string(ci.lastStatusCode) + "] reason["+ ci.lastReason +"]";
		test->connect_duration = ci.connectDuration.sec;
		test->setup_duration = ci.totalDuration.sec - ci.connectDuration.sec;
		test->result_cause_code = (int)ci.lastStatusCode;
		test->reason = ci.lastReason;
		if (ci.state == PJSIP_INV_STATE_DISCONNECTED || (test->hangup_duration && ci.connectDuration.sec >= test->hangup_duration) ){
			LOG(logINFO) << "[call] state completed duration: "<< ci.connectDuration.sec << " >= " << test->hangup_duration ;
			if (test->state != VPT_DONE) {
				if (role == 0 && test->min_mos > 0) {
					test->get_mos();
				}
				test->update_result();
			}
			if (ci.state == PJSIP_INV_STATE_CONFIRMED){
				CallOpParam prm(true);
				LOG(logINFO) << "hangup : call in PJSIP_INV_STATE_CONFIRMED" ;
				hangup(prm);
				LOG(logINFO) << "hangup ok";
			}
		}
	}
	// Create player and recorder
	if (ci.state == PJSIP_INV_STATE_CONFIRMED){
		stream_to_call(this, ci.id, remote_user.c_str());
		if (test->recording)
			record_call(this, ci.id, remote_user.c_str());
	}
	if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
		LOG(logINFO) << "[Call disconnected]";
		if (player_id != -1) {
			pjsua_player_destroy(player_id);
			player_id = -1;
		}
		if (recorder_id != -1){
			pjsua_recorder_destroy(recorder_id);
			recorder_id = -1;
		}
	}
}


/*
 * TestAccount implementation
 */

void TestAccount::setTest(Test *ptest) {
	test = ptest;
}

TestAccount::TestAccount() {
	test=NULL;
	config=NULL;
	hangup_duration=0;
	max_duration=0;
	accept_label="-";
}

TestAccount::~TestAccount() {
	LOG(logINFO) << "[Account] is being deleted: No of calls=" << calls.size() ;
}

void TestAccount::onRegState(OnRegStateParam &prm) {
	AccountInfo ai = getInfo();
	LOG(logINFO) << (ai.regIsActive? "[Register] code:" : "[Unregister] code:") << prm.code ;
	if (test) {
		if ( prm.rdata.pjRxData && prm.code != 408 && prm.code != PJSIP_SC_SERVICE_UNAVAILABLE) {
			pjsip_rx_data *pjsip_data = (pjsip_rx_data *) prm.rdata.pjRxData;
			test->transport = pjsip_data->tp_info.transport->type_name;
		}
		std::string res = "registration[" + std::to_string(prm.code) + "] reason["+ prm.reason + "] expiration[" + std::to_string(prm.expiration) +"]";
		test->result_cause_code = (int)prm.code;
		test->reason = prm.reason;
		test->update_result();
	}
}

void TestAccount::onIncomingCall(OnIncomingCallParam &iprm) {
	TestCall *call = new TestCall(*this, iprm.callId);

	pjsip_rx_data *pjsip_data = (pjsip_rx_data *) iprm.rdata.pjRxData;
	CallInfo ci = call->getInfo();
	CallOpParam prm;
	AccountInfo acc_inf = getInfo();
	LOG(logINFO) <<"[onIncomingCall]["<< acc_inf.uri <<"]["<<call->getId()<<"]from["<<ci.remoteUri<<"]to["<<ci.localUri<<"]id["<<ci.callIdString<<"]";
	if (!call->test) {
		LOG(logINFO)<<"[onIncomingCall] max call duration["<< hangup_duration <<"]";
		call->test = new Test(config);
		call->test->hangup_duration = hangup_duration;
		call->test->max_duration = max_duration;
		call->test->expected_cause_code = 200;
		call->test->local_user = ci.localUri;
		call->test->remote_user = ci.remoteUri;
		call->test->type = "accept";
		call->test->label = accept_label;
		call->test->sip_call_id = ci.callIdString;
		call->test->transport = pjsip_data->tp_info.transport->type_name;
		call->test->peer_socket = iprm.rdata.srcAddress;
		call->test->state = VPT_RUN;
	}
	calls.push_back(call);
	config->calls.push_back(call);
	prm.statusCode = (pjsip_status_code)200;
	call->answer(prm);
}


/*
 *  Test implementation
 */

Test::Test(Config *p_config){
	char now[20] = {'\0'};
	get_time_string(now);
	from="";
	to="";
	type = "unknown";
	wait_state = INV_STATE_NULL;
	state = VPT_RUN_WAIT;
	start_time = now;
	config = p_config;
	min_mos = 0.0;
	mos = 0.0;
	expected_cause_code = -1;
	result_cause_code = -1;
	reason = "";
	connect_duration = 0;
	expected_duration = 0;
	setup_duration = 0;
	max_duration = 0;
	hangup_duration = 0;
	call_id = 0;
	sip_call_id = "";
	label = "-";
	recording = false;
	playing = false;
	LOG(logINFO)<<LOG_COLOR_INFO<<"New test created !"<<LOG_COLOR_END;
}

void Test::get_mos() {
	std::string reference = "voice_ref_files/reference_8000.wav";
	std::string degraded = "voice_files/" + remote_user + "_rec.wav";
	mos = pesq_process(8000, reference.c_str(), degraded.c_str());
	LOG(logINFO) <<"[call] mos["<<mos<<"] min-mos["<<min_mos<<"] "<< reference <<" vs "<< degraded;
}

void jsonify(std::string *str) {
	size_t index = 0;
	while (true) {
		index = str->find("\"", index);
		if (index == std::string::npos) break;
		std::cout << str->substr(index)  << std::endl;
		str->replace(index, 1, "\\\"");
		index += 2;
	}
}

void Test::update_result() {
		char now[20] = {'\0'};
		bool success = false;
		get_time_string(now);
		end_time = now;
		state = VPT_DONE;
		std::string res = "FAIL";
		if (expected_duration && expected_duration != connect_duration) {
			success=false;
		} else if (max_duration && max_duration < connect_duration) {
			success=false;
		} else if(expected_cause_code == result_cause_code && mos >= min_mos) {
			res = "PASS";
			success=true;
		}

		// JSON report
		string jsonFrom = local_user;
		jsonify(&jsonFrom);
		string jsonTo = remote_user;
		jsonify(&jsonTo);
		string jsonCallid = sip_call_id;
		jsonify(&jsonCallid);
		string jsonReason = reason;
		jsonify(&jsonReason);

		config->json_result_count++;
		std::string result_line_json = "{\""+std::to_string(config->json_result_count)+"\": {"
							"\"label\": \""+label+"\", "
							"\"start\": \""+start_time+"\", "
							"\"end\": \""+end_time+"\", "
							"\"action\": \""+type+"\", "
							"\"from\": \""+jsonFrom+"\", "
							"\"to\": \""+jsonTo+"\", "
							"\"result\": \""+res+"\", "
							"\"expected_cause_code\": "+std::to_string(expected_cause_code)+", "
							"\"cause_code\": "+std::to_string(result_cause_code)+", "
							"\"reason\": \""+jsonReason+"\", "
							"\"callid\": \""+jsonCallid+"\", "
							"\"transport\": \""+transport+"\", "
							"\"peer_socket\": \""+peer_socket+"\", "
							"\"duration\": "+std::to_string(connect_duration)+", "
							"\"expected_duration\": "+std::to_string(expected_duration)+", "
							"\"max_duration\": "+std::to_string(max_duration)+", "
							"\"hangup_duration\": "+std::to_string(hangup_duration) +" "
						"}}";
		config->result_file.write(result_line_json);
		LOG(logINFO)<<"["<<now<<"]" << result_line_json;
		config->result_file.flush();

		// prepare HTML report
		std::string td_style= "style='border-color:#98B4E5;border-style:solid;padding:3px;border-width:1px;'";
		std::string td_hd_style = "style='border-color:#98B4E5;background-color: #EEF2F5;border-style:solid;padding:3px;border-width:1px;'";
		std::string td_small_style="style='padding:1px;width:50%;border-style:solid;border-spacing:0px;border-width:1px;border-color:#98B4E5;text-align:center;font-size:8pt'";
		if (config->testResults.size() == 0){
			std::string headers = "<tr>"
				              "<td "+td_hd_style+">label</td>"
				              "<td "+td_hd_style+">start/end</td>"
				              "<td "+td_hd_style+">type</td><td "+td_hd_style+">result</td>"
				              "<td "+td_hd_style+">cause code</td><td "+td_hd_style+">reason</td>"
				              "<td "+td_hd_style+">mos</td>"
				              "<td "+td_hd_style+">duration</td>"
					      "<td "+td_hd_style+">from</td><td "+td_hd_style+">to</td>\r\n";
			config->testResults.push_back(headers);
		}
		std::string mos_color = "green";
		std::string code_color = "green";
		if (expected_cause_code != result_cause_code)
			code_color = "red";
		if (mos < min_mos)
			mos_color = "red";
		if (!success)
			res = "<font color='red'>"+res+"</font>";

		std::string html_duration_table = "<table><tr><td>expected</td><td>max</td><td>hangup</td><td>connect</td></tr><tr>"
                                                  "<td "+td_small_style+">"+std::to_string(expected_duration)+"</td>"
						  "<td "+td_small_style+">"+std::to_string(max_duration)+"</td>"
						  "<td "+td_small_style+">"+std::to_string(hangup_duration)+"</td>"
						  "<td "+td_small_style+">"+std::to_string(connect_duration)+"</td></tr></table>";
		type = type +"["+std::to_string(call_id)+"]transport["+transport+"]<br>peer socket["+peer_socket+"]<br>"+sip_call_id;
		std::string result = "<tr>"
					 "<td "+td_style+">"+label+"</td>"
			                 "<td "+td_style+">"+start_time+"<br>"+end_time+"</td><td "+td_style+">"+type+"</td>"
                                         "<td "+td_style+">"+res+"</td>"
                                         "<td "+td_style+">"+std::to_string(expected_cause_code)+"|<font color="+code_color+">"+std::to_string(result_cause_code)+"</font></td>"
                                         "<td "+td_style+">"+reason+"</td>"
                                         "<td "+td_style+">"+std::to_string(min_mos)+">=<font color="+mos_color+">"+std::to_string(mos)+"</font></td>"
					 "<td "+td_style+">"+html_duration_table+"</td>"
                                         "<td "+td_style+">"+local_user+"</td>"
                                         "<td "+td_style+">"+remote_user+"</td>"
					 "</tr>\r\n";
		config->testResults.push_back(result);
}



/*
 * ResultFile implementation
 */

ResultFile::ResultFile(string name) : name(name) {
	open();
}

bool ResultFile::write(string res) {
	try {
		file << res << "\n";
	} catch (Error & err) {
		LOG(logINFO) <<__FUNCTION__<< "Exception: " << err.info() ;
		return false;
	}
	return true;
}

void ResultFile::flush() {
	file.flush();
}

bool ResultFile::open() {
	file.open(name.c_str(), std::fstream::in | std::fstream::out | std::fstream::app);
	if (file.is_open()) {
		LOG(logINFO) << "JSON result file:" << name << "\n";
	} else {
		std::cerr <<__FUNCTION__<< " [error] test can not open log file :" << name ;
		return false;
	}
}

void ResultFile::close() {
	file.close();
}


/*
 * Config implementation
 */

Config::Config(string result_fn) : result_file(result_fn), action(this) {
	json_result_count = 0;
}

void Config::log(std::string message) {
	LOG(logINFO) <<"[timestamp]"<< message ;
}

void Config::update_result(std::string text){
	char now[20] = {'\0'};
	get_time_string(now);
}

Config::~Config() {
	result_file.close();
}

void Config::removeCall(TestCall *call) {
	for (auto it = calls.begin(); it != calls.end(); ++it) {
		if (*it == call) {
			calls.erase(it);
			break;
		}
	}
	delete call;
}

TestAccount* Config::findAccount(std::string account_name) {
	for (auto & account : accounts) {
		AccountInfo acc_inf = account->getInfo();
		int proto_length = 4; // "sip:"
		if (acc_inf.uri.compare(0, 4, "sips") == 0)
			proto_length = 5;
		LOG(logINFO) << "[searching account]["<< proto_length << "]["<<acc_inf.uri<<"]<>["<<account_name<<"]";
		if (acc_inf.uri.compare(proto_length, account_name.length(), account_name) == 0) {
			LOG(logINFO) << "found account id["<< acc_inf.id <<"] uri[" << acc_inf.uri <<"]";
			return account;
		}
	}
	return NULL;
}

string get_env(string env) {
	if (const char* val = std::getenv(env.c_str())) {
		std::string s(val);
		return s;
	} else {
		return "";
	}
}

bool Config::process(std::string p_configFileName, std::string p_jsonResultFileName) {
	const char* tag = "[loading xml config] ";
	ezxml_t xml_actions, xml_action;
	configFileName = p_configFileName;
	ezxml_t xml_conf = ezxml_parse_file(configFileName.c_str());
	xml_conf_head = xml_conf; // saving the head if the linked list

	if(!xml_conf){
		std::cerr <<tag<< "[error] test can not load file :" << configFileName ;
		return false;
	} else {
		update_result("loading tests...");
	}

	for (xml_actions = ezxml_child(xml_conf, "actions"); xml_actions; xml_actions=xml_actions->next) {
		LOG(logINFO) <<tag<< xml_actions->name;
		for (xml_action = ezxml_child(xml_actions, "action"); xml_action; xml_action=xml_action->next) {
			if (!ezxml_attr(xml_action,"type")) {
				std::cerr <<" >> "<<tag<<"invalid action !\n";
				continue;
			}
			string action_type = ezxml_attr(xml_action,"type");
			LOG(logINFO) <<" >> "<<tag<<"type:"<< action_type ;

			// more refactoring needed
			std::string caller {};
			const char *p_caller = ezxml_attr(xml_action,"caller");
			if (p_caller) caller = p_caller;
			if (caller.compare(0, 7, "VP_ENV_") == 0) caller = get_env(caller);

			std::string username {};
			const char *p_username = ezxml_attr(xml_action,"username");
			if (p_username) username = p_username;
			if (username.compare(0, 7, "VP_ENV_") == 0) username = get_env(username);

			std::string password {};
			const char *p_password = ezxml_attr(xml_action,"password");
			if (p_password) password = p_password;
			if (password.compare(0, 7, "VP_ENV_") == 0) password = get_env(password);

			/* action */
			int duration_ms = 0;
			if (ezxml_attr(xml_action,"ms")) duration_ms = atoi(ezxml_attr(xml_action,"ms"));
			if ( action_type.compare("wait") == 0 ) {
				action.do_wait(false, duration_ms);
			} else if ( action_type.compare("wait-complete") == 0 || action_type.compare("sleep") == 0 ) {
				action.do_wait(true, duration_ms);
			} else if ( action_type.compare("alert") == 0 ) {
				if (!ezxml_attr(xml_action,"email")) {
					std::cerr <<" >> "<<tag<<"missing pamameter !\n";
					continue;
				}
				this->alert_email_to = ezxml_attr(xml_action,"email");
				if (ezxml_attr(xml_action,"email_from")) this->alert_email_from = ezxml_attr(xml_action,"email_from");
				if (ezxml_attr(xml_action,"smtp_host")) this->alert_server_url = ezxml_attr(xml_action,"smtp_host");
			} else if ( action_type.compare("register") == 0 ) {
				if (username.empty() || !ezxml_attr(xml_action,"realm") || password.empty() || !ezxml_attr(xml_action,"registrar")) {
					std::cerr <<" >> "<<tag<<"missing pamameter !";
					continue;
				}
				TestAccount *acc = findAccount(username);
				int found = 0;
				if (acc) {
					found = 1;
					AccountInfo acc_inf = acc->getInfo();
					LOG(logINFO) << "found: " << username <<" [not unregistered]";
				} else {
					acc = new TestAccount();
					accounts.push_back(acc);
				}

				Test *test = new Test(this);
				test->local_user = username;
				test->remote_user = username;
				if (ezxml_attr(xml_action,"label")){
					test->label = ezxml_attr(xml_action,"label");
				}

				std::string registrar = ezxml_attr(xml_action,"registrar");
				test->expected_cause_code = atoi(ezxml_attr(xml_action,"expected_cause_code"));
				test->from = username;
				test->type = action_type;

				LOG(logINFO) <<" >> "<<tag<< "sip:" + username + "@" + registrar  ;
				// register account
				AccountConfig acc_cfg;

				SipHeader sh;
				sh.hName = "User-Agent";
				sh.hValue = "<voip_patrol>";
				acc_cfg.regConfig.headers.push_back(sh);

				acc_cfg.sipConfig.transportId = transport_id_udp;
				if (ezxml_attr(xml_action,"transport")) {
					std::string transport = ezxml_attr(xml_action,"transport");
					if (transport.compare("tcp") == 0) {
						acc_cfg.sipConfig.transportId = transport_id_tcp;
					}
					if (transport.compare("tls") == 0) {
						if (transport_id_tls == -1) {
							std::cerr <<" >> "<<tag<<"TLS transport not supported " << action_type ;
							continue;
						}
						acc_cfg.sipConfig.transportId = transport_id_tls;
					}
				}
				if (acc_cfg.sipConfig.transportId == transport_id_tls) {
					acc_cfg.idUri = "sips:" + username + "@" + registrar;
					acc_cfg.regConfig.registrarUri = "sips:" + registrar;
					LOG(logINFO) << "SIPS URI Scheme";
				} else {
					LOG(logINFO) << "SIP URI Scheme";
					acc_cfg.idUri = "sip:" + username + "@" + registrar;
					acc_cfg.regConfig.registrarUri = "sip:" + registrar;
				}
				acc_cfg.sipConfig.authCreds.push_back( AuthCredInfo("digest", ezxml_attr(xml_action,"realm"), username, 0, password) );
				if (!found) {
					// acc->setTransport();
					acc->config = this;
					acc->create(acc_cfg);
				} else {
					acc->modify(acc_cfg);
				}
				acc->setTest(test);

			} else if ( action_type.compare("accept") == 0 ) {
				if (!ezxml_attr(xml_action,"account")) {
					std::cerr <<" >> "<<tag<<"missing action parameters 'account' for: " << action_type ;
					continue;
				}
				std::string account_name = ezxml_attr(xml_action,"account");
				TestAccount *acc = findAccount(account_name);
				if (!acc) {
					LOG(logINFO) << "account not found: " << account_name << " creating";
					acc = new TestAccount();
					AccountConfig acc_cfg;
					acc_cfg.sipConfig.transportId = transport_id_udp;
					if (ezxml_attr(xml_action,"transport")) {
						std::string transport = ezxml_attr(xml_action,"transport");
						if (transport.compare("tcp") == 0) {
							acc_cfg.sipConfig.transportId = transport_id_tcp;
						}
						if (transport.compare("tls") == 0) {
							if (transport_id_tls == -1) {
								std::cerr <<" >> "<<tag<<"TLS transport not supported " << action_type ;
								continue;
							}
							acc_cfg.sipConfig.transportId = transport_id_tls;
						}
					}
					if (acc_cfg.sipConfig.transportId == transport_id_tls) {
						acc_cfg.idUri = "sips:" + account_name;
						//acc_cfg.sipConfig.contactParams = ";trans=tls;";
					} else {
						acc_cfg.idUri = "sip:" + account_name;
					}
					acc->config = this;
					acc->create(acc_cfg);
				}
				if (ezxml_attr(xml_action,"hangup")){
					acc->hangup_duration = atoi(ezxml_attr(xml_action,"hangup"));
				} else {
					acc->hangup_duration = 0;
				}
				if (ezxml_attr(xml_action,"max_duration")){
					acc->max_duration = atoi(ezxml_attr(xml_action,"max_duration"));
				} else {
					acc->max_duration = 0;
				}
				if (ezxml_attr(xml_action,"label")){
					acc->accept_label = ezxml_attr(xml_action,"label");
				}
			} else if ( action_type.compare("call") == 0 ) {
				if (!ezxml_attr(xml_action,"caller") || !ezxml_attr(xml_action,"callee") ) {
					std::cerr <<" >> "<<tag<<"missing action parameters for " << action_type ;
					continue;
				}

				std::string caller = ezxml_attr(xml_action,"caller");
				std::string callee = ezxml_attr(xml_action,"callee");

				LOG(logINFO) <<" >> "<<tag<<"action parameters found : " << action_type ;
				// make call begin
				TestAccount *acc = findAccount(caller);
				std::string transport("");
				if (!acc) {
					LOG(logINFO) << "caller not found[" << caller << "] creating new account.";
					acc = new TestAccount();
					AccountConfig acc_cfg;

					acc_cfg.sipConfig.transportId = transport_id_udp;
					if (ezxml_attr(xml_action,"transport")) {
						transport = ezxml_attr(xml_action,"transport");
						if (transport.compare("tcp") == 0) {
							acc_cfg.sipConfig.transportId = transport_id_tcp;
						} else if (transport.compare("tls") == 0) {
							if (transport_id_tls == -1) {
								std::cerr <<" >> "<<tag<<"TLS transport not supported " << action_type ;
								continue;
							}
							acc_cfg.sipConfig.transportId = transport_id_tls;
						}
					}
					if (acc_cfg.sipConfig.transportId == transport_id_tls) {
						acc_cfg.idUri = "sips:" + caller;
					} else {
						acc_cfg.idUri = "sip:" + caller;
					}
					if (ezxml_attr(xml_action,"realm")) {
						if (username.empty() || password.empty()) {
							if (username.empty()) LOG(logERROR) << "[config] realm specified missing username";
							else LOG(logERROR) << "[config] realm specified missing password";
							continue;
						}
						std::string realm = ezxml_attr(xml_action,"realm");
						acc_cfg.sipConfig.authCreds.push_back( AuthCredInfo("digest", realm, username, 0, password) );
					}
					acc->config = this;
					acc->create(acc_cfg);
				}

				int repeat = 0;
				if ( ezxml_attr(xml_action,"repeat") ) {
					repeat = (call_wait_state_t)atoi(ezxml_attr(xml_action,"repeat"));
				}
				do {
					Test *test = new Test(this);
					std::size_t pos = caller.find("@");
					if (pos!=std::string::npos) {
						test->local_user = caller.substr(0, pos);
					}
					pos = callee.find("@");
					if (pos!=std::string::npos) {
						test->remote_user = callee.substr(0, pos);
					}

					TestCall *call = new TestCall(*acc);
					calls.push_back(call);
					if (ezxml_attr(xml_action,"wait_until")){
						test->wait_state = (call_wait_state_t)atoi(ezxml_attr(xml_action,"wait_until"));
					}
					if (ezxml_attr(xml_action,"mos")){
						test->min_mos = atof(ezxml_attr(xml_action,"mos"));
					}
					if (ezxml_attr(xml_action,"duration")){
						test->expected_duration = atoi(ezxml_attr(xml_action,"duration"));
					}
					if ( ezxml_attr(xml_action,"recording") ) {
							test->recording = true;
					}
					if (ezxml_attr(xml_action,"max_duration")){
						test->max_duration = atoi(ezxml_attr(xml_action,"max_duration"));
					}
					if (ezxml_attr(xml_action,"max_calling_duration")){
						test->max_calling_duration = atoi(ezxml_attr(xml_action,"max_calling_duration"));
					}
					if ( ezxml_attr(xml_action,"hangup") ) {
						test->hangup_duration = atoi(ezxml_attr(xml_action,"hangup"));
					}
					if (ezxml_attr(xml_action,"label")){
						test->label = ezxml_attr(xml_action,"label");
					}
					call->test = test;
					test->expected_cause_code = atoi(ezxml_attr(xml_action,"expected_cause_code"));
					test->from = caller;
					test->to = callee;
					test->type = action_type;
					acc->calls.push_back(call);
					CallOpParam prm(true);
					prm.opt.audioCount = 1;
					prm.opt.videoCount = 0;
					LOG(logINFO) << "call->test:" << test << " " << call->test->type;
					LOG(logINFO) << "calling :" +callee;
					if (transport.compare("tls") == 0) {
						call->makeCall("sips:"+callee, prm);
					} else if (transport.compare("tcp") == 0) {
						call->makeCall("sip:"+callee+";transport=tcp", prm);
					} else {
						call->makeCall("sip:"+callee, prm);
					}
					repeat--;
				} while (repeat >= 0);

			} else {
				std::cerr <<" >> "<<tag<<"unknown action !\n";
			}
		}
	}
}


/*
 * Alert implementation
 */

Alert::Alert(Config * p_config){
	curl = curl_easy_init();
	config = p_config;
}
void Alert::prepare(void){
//	std::string date = "Date: Mon, 29 Nov 2010 21:54:29 +1100\r\n";
//	upload_data.payload_content.push_back(date);
	alert_server_url = config->alert_server_url;
	std::string to = "To: <"+config->alert_email_to+">\r\n";
	upload_data.payload_content.push_back(to);
	std::string from = "From: <"+config->alert_email_from+">\r\n";
	upload_data.payload_content.push_back(from);
	std::string messageId = "Message-ID: <dcd7cb36-11db-487a-9f3a-e652a9458efd@rfcpedant.example.org>\r\n";
	upload_data.payload_content.push_back(messageId);
	std::string subject = "Subject: VoIP Patrol test report\r\n";
	upload_data.payload_content.push_back(subject);
	std::string content_type = "Content-type: text/html\r\n";
	upload_data.payload_content.push_back(content_type);
	std::string bodySeparator = "\r\n";
	upload_data.payload_content.push_back(bodySeparator);

	std::string tb_style = "style='font-size:8pt;font-family:\"DejaVu Sans\",Verdana;"
                                       "border-collapse:collapse;border-spacing:0px;"
                                       "border-style:solid;border-width:1px;text-align:center;'";
	std::string html_start = "<html><table "+tb_style+">";
	upload_data.payload_content.push_back(html_start);
	for (auto & testResult : config->testResults) {
		upload_data.payload_content.push_back(testResult);
	}
	std::string html_end = "</table></html>\n\r";
	upload_data.payload_content.push_back(html_end);
}

void Alert::send(void) {
	CURLcode res = CURLE_OK;
	struct curl_slist *recipients = NULL;
	upload_data.lines_read = 0;
	if (config->alert_server_url.empty() || config->alert_email_to.empty() || config->alert_email_from.empty())
		return;
	prepare();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, alert_server_url.c_str());
		curl_easy_setopt(curl, CURLOPT_MAIL_FROM, alert_email_from.c_str());
		recipients = curl_slist_append(recipients, config->alert_email_to.c_str());
		curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, &Alert::payload_source);
		curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		res = curl_easy_perform(curl);
		if(res != CURLE_OK)
		std::cerr << "curl_easy_perform() failed: "  << curl_easy_strerror(res) << alert_server_url <<"\n";
		curl_slist_free_all(recipients);
		curl_easy_cleanup(curl);
	}
	LOG(logINFO) << "email alert sent...\n";
}

size_t Alert::payload_source(void *ptr, size_t size, size_t nmemb, void *userp) {
	upload_data_t *upload_data = (upload_data_t *)userp;
	const char *data;

	if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
		return 0;
	}

	if(upload_data->lines_read >= upload_data->payload_content.size())
		return 0;
	data = upload_data->payload_content[upload_data->lines_read].c_str();
	if(data) {
		size_t len = strlen(data);
		memcpy(ptr, data, len);
		upload_data->lines_read++;
		return len;
	}

	return 0;
}


int main(int argc, char **argv){
	int ret = 0;

	pjsip_cfg_t *pjsip_config = pjsip_cfg();
	std::cout <<"pjsip_config->tsx.t1 :" << pjsip_config->tsx.t1 <<"\n";
	// pjsip_config->tsx.t1 = 250;
	// pjsip_config->tsx.t2 = 250;
	// pjsip_config->tsx.t4 = 1000;

	pjsip_cfg()->endpt.disable_secure_dlg_check = 1;

	Endpoint ep;

	std::string conf_fn = "conf.xml";
	std::string log_fn = "";
	std::string log_test_fn = "results.json";
	int port = 5070;
	int log_level_console = 2;
	int log_level_file = 10;
	Config config(log_test_fn);

	// command line argument
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if ((arg == "-h") || (arg == "--help")) {
			std::cout <<"\n"<< argv[0] <<"                                \n"\
            " -v --vesion                       voip_patrol version       \n"\
            " --log-level-file <0-10>           file log level            \n"\
            " --log-level-console <0-10>        console log level         \n"\
            " -p --port <5060>                  local port                \n"\
            " -c,--conf <conf.xml>              XML scenario file         \n"\
            " -l,--log <logfilename>            voip_patrol log file name \n"\
            " -o,--output <result.json>         json result file name     \n"\
			"                                                             \n";
			return 0;
		} else if ( (arg == "-v") || (arg == "--version") ) {
			std::cout <<"version: voip_patrol "<<VERSION<<std::endl;
			return 0;
		} else if ( (arg == "-c") || (arg == "--conf") ) {
			if (i + 1 < argc) {
				conf_fn = argv[++i];
			}
		} else if ( (arg == "--log-level-file") ) {
			if (i + 1 < argc) {
				log_level_file = atoi(argv[++i]);
			}
		} else if ( (arg == "--log-level-console") ) {
			if (i + 1 < argc) {
				log_level_console = atoi(argv[++i]);
			}
		} else if ( (arg == "-l") || (arg == "--log")) {
			if (i + 1 < argc) {
				log_fn = argv[++i];
			}
		} else if ( (arg == "-p") || (arg == "--port")) {
			if (i + 1 < argc) {
				port = atoi(argv[++i]);
			}
		} else if ( (arg == "-o") || (arg == "--output")) {
			if (i + 1 < argc) {
				log_test_fn = argv[++i];
			}
		}
	}

	FILELog::ReportingLevel() = (TLogLevel)log_level_console;
	if ( log_fn.length() > 0 ) {
		FILELog::ReportingLevel() = logDEBUG3;
		FILE* log_fd = fopen(log_fn.c_str(), "w");
		Output2FILE::Stream() = log_fd;
	}
	std::cout << "\n* * * * * * *\n "
		"voip_patrol version: "<<VERSION<<"\n"
		"configuration: "<<conf_fn<<"\n"
		"log file: "<<log_fn<<"\n"
		"output file: "<<log_test_fn<<"\n"
		"* * * * * * *\n";

	TransportConfig tcfg;
	try {
		ep.libCreate();
		EpConfig ep_cfg;
		ep_cfg.uaConfig.maxCalls = 1000;
		ep_cfg.logConfig.level = log_level_file;
		ep_cfg.logConfig.consoleLevel = log_level_console;
		std::string pj_log_fn =  "pjsua_" + std::to_string(port) + ".log";
		ep_cfg.logConfig.filename = pj_log_fn.c_str();
		ep_cfg.medConfig.ecTailLen = 0; // disable echo canceller
		ep_cfg.medConfig.noVad = 1;

		ep.libInit( ep_cfg );
		// pjsua_set_null_snd_dev() before calling pjsua_start().

		// TCP and UDP transports

		tcfg.port = port;
		config.transport_id_tcp = ep.transportCreate(PJSIP_TRANSPORT_TCP, tcfg);
		tcfg.port = port;
		config.transport_id_udp = ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
	} catch (Error & err) {
		LOG(logINFO) << "Exception: " << err.info() ;
		return 1;
	}

	try {
		// TLS transport
		tcfg.port = port+1;
		// Optional, set CA/certificate/private key files.
		tcfg.tlsConfig.CaListFile = "certificate.pem";
		tcfg.tlsConfig.certFile = "cert.pem";
		tcfg.tlsConfig.privKeyFile = "key.pem";
		tcfg.tlsConfig.verifyServer = 0;
		tcfg.tlsConfig.verifyClient = 0;
		// Optional, set ciphers. You can select a certain cipher/rearrange the order of ciphers here.
		// tcfg.ciphers = ep->utilSslGetAvailableCiphers();
		config.transport_id_tls = ep.transportCreate(PJSIP_TRANSPORT_TLS, tcfg);
		LOG(logINFO) << "TLS supported ";
	} catch (Error & err) {
		config.transport_id_tls = -1;
		LOG(logINFO) << "Exception: TLS not supported, see README. " << err.info() ;
	}

	try {
		// load config and execute test
		pjsua_set_null_snd_dev();
		ep.libStart();

		config.process(conf_fn, log_test_fn);

		LOG(logINFO) << "wait complete all...";
		config.action.do_wait(true);

		LOG(logINFO) << "checking alerts...";

		// send email reporting
		Alert alert(&config);
		alert.send();

		LOG(logINFO) << "hangup all calls..." ;
		ep.hangupAllCalls();

		ret = PJ_SUCCESS;
	} catch (Error &err) {
		LOG(logINFO) << "Exception: " << err.info() ;
		ret = 1;
	}


	try {
		ep.libDestroy();
	} catch (Error &err) {
		LOG(logINFO) << "Exception: " << err.info() ;
		ret = 1;
	}

	if (ret == PJ_SUCCESS) {
		LOG(logINFO) << "Success" ;
	} else {
		LOG(logINFO) << "Error Found" ;
	}

	LOG(logINFO) << "exiting !" ;
	return ret;
}


