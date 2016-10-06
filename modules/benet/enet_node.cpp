
#include "modules/benet/enet_node.h"

void ENetNode::_notification(int p_what) {
	if(!is_inside_tree() || get_tree()->is_editor_hint())
		return;

	if(p_what == NOTIFICATION_ENTER_TREE) {
		get_tree()->connect("idle_frame", this, "_on_idle_frame");
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		get_tree()->disconnect("idle_frame", this, "_on_idle_frame");
	}
}

void ENetNode::_on_idle_frame() {
	if(!network_peer.is_valid())
		return;

	_network_poll();
}

void ENetNode::set_network_peer(const Ref<ENetPacketPeer>& p_network_peer) {
	if (network_peer.is_valid()) {
		network_peer->disconnect("peer_connected",this,"_network_peer_connected");
		network_peer->disconnect("peer_disconnected",this,"_network_peer_disconnected");
		network_peer->disconnect("connection_succeeded",this,"_connected_to_server");
		network_peer->disconnect("connection_failed",this,"_connection_failed");
		network_peer->disconnect("server_disconnected",this,"_server_disconnected");
		connected_peers.clear();
		//path_get_cache.clear();
		//path_send_cache.clear();
		//last_send_cache_id=1;
	}

	ERR_EXPLAIN("Supplied NetworkedNetworkPeer must be connecting or connected.");
	ERR_FAIL_COND(p_network_peer.is_valid() && p_network_peer->get_connection_status()==NetworkedMultiplayerPeer::CONNECTION_DISCONNECTED);

	network_peer=p_network_peer;

	if (network_peer.is_valid()) {
		network_peer->connect("peer_connected",this,"_network_peer_connected");
		network_peer->connect("peer_disconnected",this,"_network_peer_disconnected");
		network_peer->connect("connection_succeeded",this,"_connected_to_server");
		network_peer->connect("connection_failed",this,"_connection_failed");
		network_peer->connect("server_disconnected",this,"_server_disconnected");
	}
}

void ENetNode::_network_peer_connected(int p_id) {

	connected_peers.insert(p_id);
	//path_get_cache.insert(p_id,PathGetCache());
	emit_signal("network_peer_connected",p_id);
}

void ENetNode::_network_peer_disconnected(int p_id) {

	connected_peers.erase(p_id);
	//path_get_cache.erase(p_id); //I no longer need your cache, sorry
	emit_signal("network_peer_disconnected",p_id);
}

void ENetNode::_connected_to_server() {

	emit_signal("connected_to_server");
}

void ENetNode::_connection_failed() {

	emit_signal("connection_failed");
}

void ENetNode::_server_disconnected() {

	emit_signal("server_disconnected");
}

void ENetNode::_network_poll() {

	if (!network_peer.is_valid() || network_peer->get_connection_status()==NetworkedMultiplayerPeer::CONNECTION_DISCONNECTED)
		return;

	network_peer->poll();

	if (!network_peer.is_valid()) //it's possible that polling might have resulted in a disconnection, so check here
		return;

	while(network_peer->get_available_packet_count()) {

		int sender = network_peer->get_packet_peer();
		int channel = network_peer->get_packet_channel();

		if(channel==-1) {

			int len;
			const uint8_t *packet;

			Error err = network_peer->get_packet(&packet,len);
			if (err!=OK) {
				ERR_PRINT("Error getting packet!");
			}

			_network_process_packet(sender,packet,len);

		} else {

			DVector<uint8_t> pkt;

			Error err = network_peer->get_packet_buffer(pkt);
			if (err!=OK) {
				ERR_PRINT("Error getting packet!");
			}

			emit_signal("network_peer_packet", sender, channel, pkt);

		}

		if (!network_peer.is_valid()) {
			break; //it's also possible that a packet or RPC caused a disconnection, so also check here
		}
	}


}

void ENetNode::_network_process_packet(int p_from, const uint8_t* p_packet, int p_packet_len) {
	// Not implemented yet!
/*
	ERR_FAIL_COND(p_packet_len<5);

	uint8_t packet_type = p_packet[0];

	switch(packet_type) {

		case NETWORK_COMMAND_REMOTE_CALL:
		case NETWORK_COMMAND_REMOTE_SET: {

			ERR_FAIL_COND(p_packet_len<5);
			uint32_t target = decode_uint32(&p_packet[1]);


			Node *node=NULL;

			if (target&0x80000000) {

				int ofs = target&0x7FFFFFFF;
				ERR_FAIL_COND(ofs>=p_packet_len);

				String paths;
				paths.parse_utf8((const char*)&p_packet[ofs],p_packet_len-ofs);

				NodePath np = paths;

				node = get_root()->get_node(np);
				if (node==NULL) {
					ERR_EXPLAIN("Failed to get path from RPC: "+String(np));
					ERR_FAIL_COND(node==NULL);
				}
			} else {

				int id = target;

				Map<int,PathGetCache>::Element *E=path_get_cache.find(p_from);
				ERR_FAIL_COND(!E);

				Map<int,PathGetCache::NodeInfo>::Element *F=E->get().nodes.find(id);
				ERR_FAIL_COND(!F);

				PathGetCache::NodeInfo *ni = &F->get();
				//do proper caching later

				node = get_root()->get_node(ni->path);
				if (node==NULL) {
					ERR_EXPLAIN("Failed to get cached path from RPC: "+String(ni->path));
					ERR_FAIL_COND(node==NULL);
				}


			}

			ERR_FAIL_COND(p_packet_len<6);

			//detect cstring end
			int len_end=5;
			for(;len_end<p_packet_len;len_end++) {
				if (p_packet[len_end]==0) {
					break;
				}
			}

			ERR_FAIL_COND(len_end>=p_packet_len);

			StringName name = String::utf8((const char*)&p_packet[5]);




			if (packet_type==NETWORK_COMMAND_REMOTE_CALL) {

				if (!node->can_call_rpc(name))
					return;

				int ofs = len_end+1;

				ERR_FAIL_COND(ofs>=p_packet_len);

				int argc = p_packet[ofs];
				Vector<Variant> args;
				Vector<const Variant*> argp;
				args.resize(argc);
				argp.resize(argc);

				ofs++;

				for(int i=0;i<argc;i++) {

					ERR_FAIL_COND(ofs>=p_packet_len);
					int vlen;
					Error err = decode_variant(args[i],&p_packet[ofs],p_packet_len-ofs,&vlen);
					ERR_FAIL_COND(err!=OK);
					//args[i]=p_packet[3+i];
					argp[i]=&args[i];
					ofs+=vlen;
				}

				Variant::CallError ce;

				node->call(name,argp.ptr(),argc,ce);
				if (ce.error!=Variant::CallError::CALL_OK) {
					String error = Variant::get_call_error_text(node,name,argp.ptr(),argc,ce);
					error="RPC - "+error;
					ERR_PRINTS(error);
				}

			} else {

				if (!node->can_call_rset(name))
					return;

				int ofs = len_end+1;

				ERR_FAIL_COND(ofs>=p_packet_len);

				Variant value;
				decode_variant(value,&p_packet[ofs],p_packet_len-ofs);

				bool valid;

				node->set(name,value,&valid);
				if (!valid) {
					String error = "Error setting remote property '"+String(name)+"', not found in object of type "+node->get_type();
					ERR_PRINTS(error);
				}
			}

		} break;
		case NETWORK_COMMAND_SIMPLIFY_PATH: {

			ERR_FAIL_COND(p_packet_len<5);
			int id = decode_uint32(&p_packet[1]);

			String paths;
			paths.parse_utf8((const char*)&p_packet[5],p_packet_len-5);

			NodePath path = paths;

			if (!path_get_cache.has(p_from)) {
				path_get_cache[p_from]=PathGetCache();
			}

			PathGetCache::NodeInfo ni;
			ni.path=path;
			ni.instance=0;

			path_get_cache[p_from].nodes[id]=ni;


			{
				//send ack

				//encode path
				CharString pname = String(path).utf8();
				int len = encode_cstring(pname.get_data(),NULL);

				Vector<uint8_t> packet;

				packet.resize(1+len);
				packet[0]=NETWORK_COMMAND_CONFIRM_PATH;
				encode_cstring(pname.get_data(),&packet[1]);

				network_peer->set_transfer_mode(NetworkedMultiplayerPeer::TRANSFER_MODE_RELIABLE);
				network_peer->set_target_peer(p_from);
				network_peer->put_packet(packet.ptr(),packet.size(),0);
			}
		} break;
		case NETWORK_COMMAND_CONFIRM_PATH: {

			String paths;
			paths.parse_utf8((const char*)&p_packet[1],p_packet_len-1);

			NodePath path = paths;

			PathSentCache *psc = path_send_cache.getptr(path);
			ERR_FAIL_COND(!psc);

			Map<int,bool>::Element *E=psc->confirmed_peers.find(p_from);
			ERR_FAIL_COND(!E);
			E->get()=true;
		} break;
	}
*/
}

void ENetNode::_bind_methods() {
	ADD_SIGNAL( MethodInfo("network_peer_connected",PropertyInfo(Variant::INT,"id")));
	ADD_SIGNAL( MethodInfo("network_peer_disconnected",PropertyInfo(Variant::INT,"id")));
	ADD_SIGNAL( MethodInfo("network_peer_packet",PropertyInfo(Variant::INT,"peer"),PropertyInfo(Variant::INT,"channel"),PropertyInfo(Variant::RAW_ARRAY,"packet")));
	ADD_SIGNAL( MethodInfo("connected_to_server"));
	ADD_SIGNAL( MethodInfo("connection_failed"));
	ADD_SIGNAL( MethodInfo("server_disconnected"));

	ObjectTypeDB::bind_method(_MD("_on_idle_frame"),&ENetNode::_on_idle_frame);
	ObjectTypeDB::bind_method(_MD("set_network_peer","peer:ENetPacketPeer"),&ENetNode::set_network_peer);
	ObjectTypeDB::bind_method(_MD("_network_peer_connected"),&ENetNode::_network_peer_connected);
	ObjectTypeDB::bind_method(_MD("_network_peer_disconnected"),&ENetNode::_network_peer_disconnected);
	ObjectTypeDB::bind_method(_MD("_connected_to_server"),&ENetNode::_connected_to_server);
	ObjectTypeDB::bind_method(_MD("_connection_failed"),&ENetNode::_connection_failed);
	ObjectTypeDB::bind_method(_MD("_server_disconnected"),&ENetNode::_server_disconnected);
}

ENetNode::ENetNode() {
	network_peer = Ref<ENetPacketPeer>();
	// Pass
}

ENetNode::~ENetNode() {
	// Pass
}


