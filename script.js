// This script takes the incoming message (msg) from your device
// and transforms it into a clean JSON object that matches your Supabase table.

var newPayload = {
    // ⬇️ ADD THIS LINE ⬇️
    machine_id: msg.machine_id, 
    
    total_Count: msg.total_Count,
    batch: msg.batch,
    Flavor: msg.Flavor,
    
    // We add a server-side timestamp for accurate logging.
    Timestamp: new Date().toISOString()
};

// The returned 'msg' will be the JSON object sent to the REST API node.
return {msg: newPayload, metadata: metadata, msgType: msgType};
