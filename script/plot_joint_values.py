import os
import numpy as np
import pandas as pd

import matplotlib.pyplot as plt

from read_plot_utils import read_jsid_bag

CONTROLLER_NAME = 'ctrl_joint_space_ID'
# DIRECTORY = '/home/mfourmy/Downloads/franka_experiments_31_01_23'

# BAG_NAME = 'ctrl_joint_space_ID_franka_lowstiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_franka_lowstiff.yaml'
# BAG_NAME = 'ctrl_joint_space_ID_franka_midstiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_franka_midstiff.yaml'
# BAG_NAME = 'ctrl_joint_space_ID_franka_stiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_franka_stiff.yaml'
# BAG_NAME = 'ctrl_joint_space_ID_pin_lowstiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_pin_lowstiff.yaml'
# BAG_NAME = 'ctrl_joint_space_ID_pin_midstiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_pin_midstiff.yaml'
# BAG_NAME = 'ctrl_joint_space_ID_pin_stiff.bag'
# YML_NAME = 'ctrl_joint_space_ID_pin_stiff.yaml'

# DIRECTORY = '/media/mfourmy/MEDUSB/Panda_expe'
# BAG_NAMES = [
#   'ctrl_joint_space_ID_franka_stiff.bag',
#   'ctrl_joint_space_ID_pin_stiff.bag',
# ]

# DIRECTORY = '../bags/'
# BAG_NAMES = [
#   'ctrl_joint_space_ID_LONG_expe.bag',
#   # 'ctrl_joint_space_ID_lowK_NoSat.bag',
#   'ctrl_joint_space_ID_lowK_Sat.bag',
# ]

DIRECTORY = '/home/mfourmy/Downloads/Panda_Expe_BIS'

BAG_NAMES = [
  'ctrl_joint_space_ID_franka_stiff.bag',
  'ctrl_joint_space_ID_pin_stiff.bag',
]


BAG_PATHS = [os.path.join(DIRECTORY, name) for name in BAG_NAMES]





JOINTS_TO_PLOT = [1,1,1,1,1,1,1]
COLORS = 'rgbcmyk'
MSIZE = 5


fields = ['error', 'measured', 'commanded']
# fields = ['error', 'measured']
# fields = ['error']

for i_field, field in enumerate(fields):
  fig_dq, ax_dq = plt.subplots(1,1)
  fig_tau, ax_tau = plt.subplots(1,1)
  fig_q, ax_q = plt.subplots(1,1)


  fig_q.canvas.manager.set_window_title(f'Joint configurations {field}')
  fig_dq.canvas.manager.set_window_title(f'Joint velocities {field}')
  fig_tau.canvas.manager.set_window_title(f'Joint torques {field}')


  d_res = read_jsid_bag(BAG_PATHS[0], CONTROLLER_NAME)
  for i in range(7):
      if not JOINTS_TO_PLOT[i]: continue
      c = COLORS[i]
      sym = '.'
      ax_q.plot(d_res['t'], d_res['q'][field][:,i], f'{c}{sym}', label=f'q{i}', markersize=MSIZE)
      ax_dq.plot(d_res['t'], d_res['dq'][field][:,i], f'{c}{sym}', label=f'dq{i}', markersize=MSIZE)
      ax_tau.plot(d_res['t'], d_res['tau'][field][:,i], f'{c}{sym}', label=f'tau{i}', markersize=MSIZE)

  d_res = read_jsid_bag(BAG_PATHS[1], CONTROLLER_NAME)
  for i in range(7):
      if not JOINTS_TO_PLOT[i]: continue
      c = COLORS[i]
      sym = 'x'
      ax_q.plot(d_res['t'], d_res['q'][field][:,i], f'{c}{sym}', label=f'q{i}', markersize=MSIZE)
      ax_dq.plot(d_res['t'], d_res['dq'][field][:,i], f'{c}{sym}', label=f'dq{i}', markersize=MSIZE)
      ax_tau.plot(d_res['t'], d_res['tau'][field][:,i], f'{c}{sym}', label=f'tau{i}', markersize=MSIZE)


  ax_q.set_title(field)
  ax_dq.set_title(field)
  ax_tau.set_title(field)
  ax_q.set_xlabel('t (s)')
  ax_dq.set_xlabel('t (s)')
  ax_tau.set_xlabel('t (s)')
  ax_q.set_ylabel('q (rad)')
  ax_dq.set_ylabel('dq (rad/s)')
  ax_tau.set_ylabel('tau (N.m)')

  ax_q.grid()
  ax_dq.grid()
  ax_tau.grid()
  ax_q.legend()
  ax_dq.legend()
  ax_tau.legend()

plt.show()