
# TicTacToe Online - Server/Client 
Program that launches a local server designed to host and facilate the gameplay of TicTacToe.

- Multithreaded to allow  for multiple games to be run simultaneously
- Allows for Asyncronous play, making it so one user does not need to wait  for his opponent to give an Input
- Follows a custom protocol to ensure the validity of each move and monitor board state
- Detects and reports any malformed packet's sent to the server to avoid malicious actors
- Requires users to connect using unique username, utilizes mutex locks to preserve integrity of data amongst shared resources for every thread


