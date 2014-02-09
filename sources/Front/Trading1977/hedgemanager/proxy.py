
import trdk
from trdkfront.wsgi import application


# noinspection PyCallByClass,PyTypeChecker
class Proxy(trdk.Strategy):

    def __init__(self, param):
        trdk.Strategy.__init__(self, param)
        self.account = None
        self.symbol1 = None
        self.symbol2 = None
        self.tradeSize = None
        application.fullTradeStrategyList.append(self)

    def onBrokerPositionUpdate(self, security, qty, isInitial):

        if isInitial is False:
            return

        if qty < 0:
            position = trdk.ShortPosition(self, security, qty, 0)
        elif qty > 0:
            position = trdk.LongPosition(self, security, qty, 0)
        else:
            return

        self.log.debug(
            'Restoring {0} {1} position: {2}...'
            .format(
                position.type,
                position.security.symbol,
                position.activeQty))
        position.restoreOpenState()
