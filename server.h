//
//  server.h
//  Server
//
//  Created by Oleg Zdornyy on 2014-10-31.
//  Copyright (c) 2014 Oleg Zdornyy. All rights reserved.
//

#ifndef Server_server_h
#define Server_server_h

// Blocking calls to increase/decrease the available
// connection semaphore offered by the server
int decrease_connection_sem();
void post_connection_sem();
Transaction *parseRequest(const char *request);




#endif
