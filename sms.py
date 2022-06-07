from twilio.rest import Client
from sys import argv
from os import urandom
import random
import sys
 
 
account_sid = 'AC702ab7c62f653c27c5c0ed45a5c45865' 
auth_token = 'fcfab681903a91d4fd1edbc51d219b2c' 
client = Client(account_sid, auth_token) 

print('Entrei no script') 

phone = sys.argv[1]

senha = sys.argv[2]

    

message = client.messages.create(  
                              messaging_service_sid='MGe780a398fc76f28a38c00ffb4c9ea772', 
                              body='Senha ' + senha,      
                              to= phone
                          ) 

